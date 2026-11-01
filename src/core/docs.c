#include "docs.h"
#include "options.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
  char *p;
  size_t n, cap;
} sbuf;

static void sb_add(sbuf *s, const char *d, size_t len)
{
  if(s->n + len + 1 > s->cap) {
    size_t cap = s->cap ? s->cap * 2 : 128;
    char *grown;
    while(cap < s->n + len + 1)
      cap *= 2;
    grown = realloc(s->p, cap);
    if(!grown)
      return;
    s->p = grown;
    s->cap = cap;
  }
  memcpy(s->p + s->n, d, len);
  s->n += len;
  s->p[s->n] = '\0';
}

static doc_block *new_block(doc_t *d, block_kind kind)
{
  doc_block *grown = realloc(d->blocks, sizeof(doc_block) * (size_t)(d->nblocks + 1));
  doc_block *b;
  if(!grown)
    return NULL;
  d->blocks = grown;
  b = &d->blocks[d->nblocks++];
  b->kind = kind;
  b->spans = NULL;
  b->nspans = 0;
  return b;
}

static void add_span(doc_block *b, span_kind kind, const char *text, size_t len, int opt)
{
  doc_span *grown;
  char *copy;
  if(!len)
    return;
  /* merge adjacent plain text so word wrap sees one run */
  if(kind == SPAN_TEXT && b->nspans && b->spans[b->nspans - 1].kind == SPAN_TEXT) {
    doc_span *last = &b->spans[b->nspans - 1];
    size_t old = strlen(last->text);
    char *more = realloc(last->text, old + len + 1);
    if(!more)
      return;
    memcpy(more + old, text, len);
    more[old + len] = '\0';
    last->text = more;
    return;
  }
  grown = realloc(b->spans, sizeof(doc_span) * (size_t)(b->nspans + 1));
  if(!grown)
    return;
  b->spans = grown;
  copy = malloc(len + 1);
  if(!copy)
    return;
  memcpy(copy, text, len);
  copy[len] = '\0';
  b->spans[b->nspans].kind = kind;
  b->spans[b->nspans].text = copy;
  b->spans[b->nspans].opt = opt;
  b->nspans++;
}

static int is_opt_char(char c)
{
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
         c == '-' || c == '.';
}

/* Length of "--known-option" at s, or 0. Fills *opt. */
static int match_link(const char *s, const char *start, int *opt)
{
  int n = 2;
  if(s[0] != '-' || s[1] != '-' || !is_opt_char(s[2]) || s[2] == '-')
    return 0;
  if(s > start && (is_opt_char(s[-1]) || s[-1] == '\\'))
    return 0;
  while(is_opt_char(s[n]))
    n++;
  while(n > 2 && (s[n - 1] == '.' || s[n - 1] == '-'))
    n--;  /* sentence punctuation */
  *opt = options_find_n(s + 2, n - 2);
  return *opt >= 0 ? n : 0;
}

static void flush_text(doc_block *b, sbuf *t, span_kind kind)
{
  if(t->n) {
    add_span(b, kind, t->p, t->n, -1);
    t->n = 0;
    t->p[0] = '\0';
  }
}

/* Inline markup for one logical paragraph. */
static void parse_inline(doc_block *b, const char *s)
{
  const char *start = s;
  sbuf t;
  memset(&t, 0, sizeof(t));
  while(*s) {
    int opt = -1, n;
    if(*s == '\\' && s[1]) {
      sb_add(&t, s + 1, 1);
      s += 2;
    }
    else if(*s == '`') {
      const char *e = strchr(s + 1, '`');
      if(e) {
        int whole = 0;
        flush_text(b, &t, SPAN_TEXT);
        /* `--option` in backticks is still a link */
        n = match_link(s + 1, s + 1, &opt);
        if(n && s + 1 + n == e)
          whole = 1;
        if(whole)
          add_span(b, SPAN_LINK, s + 1, (size_t)n, opt);
        else
          add_span(b, SPAN_CODE, s + 1, (size_t)(e - s - 1), -1);
        s = e + 1;
      }
      else {
        sb_add(&t, s, 1);
        s++;
      }
    }
    else if(s[0] == '*' && s[1] == '*') {
      const char *e = strstr(s + 2, "**");
      if(e) {
        const char *q = s + 2;
        sbuf bold;
        memset(&bold, 0, sizeof(bold));
        flush_text(b, &t, SPAN_TEXT);
        while(q < e) {
          if(*q == '\\' && q + 1 < e)
            q++;
          sb_add(&bold, q, 1);
          q++;
        }
        flush_text(b, &bold, SPAN_BOLD);
        free(bold.p);
        s = e + 2;
      }
      else {
        sb_add(&t, s, 1);
        s++;
      }
    }
    else if(*s == '*' && s[1] && s[1] != ' ' && strchr(s + 1, '*')) {
      /* *emphasis*: shown as bold, there is no italic face */
      const char *e = strchr(s + 1, '*');
      flush_text(b, &t, SPAN_TEXT);
      add_span(b, SPAN_BOLD, s + 1, (size_t)(e - s - 1), -1);
      s = e + 1;
    }
    else if((n = match_link(s, start, &opt)) != 0) {
      flush_text(b, &t, SPAN_TEXT);
      add_span(b, SPAN_LINK, s, (size_t)n, opt);
      s += n;
    }
    else {
      sb_add(&t, s, 1);
      s++;
    }
  }
  flush_text(b, &t, SPAN_TEXT);
  free(t.p);
}

int docs_parse(const char *md, doc_t *out)
{
  const char *line = md;
  sbuf para, code;
  block_kind para_kind = BLOCK_PARA;
  memset(out, 0, sizeof(*out));
  memset(&para, 0, sizeof(para));
  memset(&code, 0, sizeof(code));

#define FLUSH_PARA()                                  \
  do {                                                \
    if(para.n) {                                      \
      doc_block *b_ = new_block(out, para_kind);      \
      if(b_)                                          \
        parse_inline(b_, para.p);                     \
      para.n = 0;                                     \
      para.p[0] = '\0';                               \
    }                                                 \
    para_kind = BLOCK_PARA;                           \
  } while(0)
#define FLUSH_CODE()                                  \
  do {                                                \
    if(code.n) {                                      \
      doc_block *b_ = new_block(out, BLOCK_CODE);     \
      while(code.n && code.p[code.n - 1] == '\n')     \
        code.n--;                                     \
      if(b_)                                          \
        add_span(b_, SPAN_CODE, code.p, code.n, -1);  \
      code.n = 0;                                     \
      code.p[0] = '\0';                               \
    }                                                 \
  } while(0)

  while(*line) {
    const char *eol = strchr(line, '\n');
    size_t len = eol ? (size_t)(eol - line) : strlen(line);
    if(!len) {
      FLUSH_PARA();
      /* a blank line inside a code block stays in it */
      if(code.n)
        sb_add(&code, "\n", 1);
    }
    else if(len >= 4 && !memcmp(line, "    ", 4) && !para.n) {
      sb_add(&code, line + 4, len - 4);
      sb_add(&code, "\n", 1);
    }
    else {
      FLUSH_CODE();
      if(line[0] == '#') {
        size_t h = 0;
        FLUSH_PARA();
        while(h < len && (line[h] == '#' || line[h] == ' '))
          h++;
        sb_add(&para, line + h, len - h);
        para_kind = BLOCK_TAG;
        FLUSH_PARA();
      }
      else if(len > 2 && (line[0] == '-' || line[0] == '*') && line[1] == ' ') {
        FLUSH_PARA();
        para_kind = BLOCK_BULLET;
        sb_add(&para, line + 2, len - 2);
      }
      else {
        size_t skip = 0;
        while(skip < len && line[skip] == ' ')
          skip++;
        if(para.n)
          sb_add(&para, " ", 1);
        sb_add(&para, line + skip, len - skip);
      }
    }
    if(!eol)
      break;
    line = eol + 1;
  }
  FLUSH_PARA();
  FLUSH_CODE();
  free(para.p);
  free(code.p);
  return 0;
#undef FLUSH_PARA
#undef FLUSH_CODE
}

void docs_free(doc_t *d)
{
  int i, j;
  for(i = 0; i < d->nblocks; i++) {
    for(j = 0; j < d->blocks[i].nspans; j++)
      free(d->blocks[i].spans[j].text);
    free(d->blocks[i].spans);
  }
  free(d->blocks);
  d->blocks = NULL;
  d->nblocks = 0;
}
