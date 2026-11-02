#include "yaml.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NODE_BUDGET 1000000L

typedef struct {
  char *name;
  jv *value;
  long nodes;
} anchor;

typedef struct {
  const char *s;
  size_t n, i;
  int line;
  size_t line_start;
  char *err;
  size_t errn;
  int failed;
  long budget;
  anchor *anchors;
  int nanchors;
  int after_key;  /* the value is on the line of its key, where a mapping cannot start */
} yp;

typedef struct {
  char *s;
  size_t n, cap;
} sb;

static void sb_add(sb *b, const char *t, size_t n)
{
  if(b->n + n + 1 > b->cap) {
    size_t want = (b->n + n + 1) * 2;
    char *g = realloc(b->s, want);
    if(!g)
      return;
    b->s = g;
    b->cap = want;
  }
  memcpy(b->s + b->n, t, n);
  b->n += n;
  b->s[b->n] = '\0';
}

static void sb_ch(sb *b, char c)
{
  sb_add(b, &c, 1);
}

static jv *fail(yp *p, const char *fmt, ...)
{
  if(!p->failed) {
    char msg[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    if(p->err && p->errn)
      snprintf(p->err, p->errn, "line %d column %d: %s", p->line, (int)(p->i - p->line_start) + 1, msg);
    p->failed = 1;
  }
  return NULL;
}

static char cur(const yp *p)
{
  return p->i < p->n ? p->s[p->i] : '\0';
}

static char at(const yp *p, size_t k)
{
  return p->i + k < p->n ? p->s[p->i + k] : '\0';
}

static void newline(yp *p)
{
  p->i++;
  p->line++;
  p->line_start = p->i;
}

static int col(const yp *p)
{
  return (int)(p->i - p->line_start);
}

static int blank_or_end(char c)
{
  return c == ' ' || c == '\n' || c == '\0' || c == '\t';
}

static int doc_marker(const yp *p)
{
  if(col(p))
    return 0;
  if((at(p, 0) == '-' && at(p, 1) == '-' && at(p, 2) == '-') || (at(p, 0) == '.' && at(p, 1) == '.' && at(p, 2) == '.'))
    return blank_or_end(at(p, 3));
  return 0;
}

/* To the first thing that means something, on this line or a later one. Its column, or -1 at the
   end of the input or of the document. */
static int next_content(yp *p)
{
  for(;;) {
    int line_begin = p->i == p->line_start;
    while(cur(p) == ' ')
      p->i++;
    if(cur(p) == '\t' && line_begin && at(p, 1) != '\n' && at(p, 1) != '\0') {
      fail(p, "a tab cannot indent");
      return -1;
    }
    while(cur(p) == ' ' || cur(p) == '\t')
      p->i++;
    if(cur(p) == '#')
      while(p->i < p->n && cur(p) != '\n')
        p->i++;
    if(p->i >= p->n)
      return -1;
    if(cur(p) == '\n') {
      newline(p);
      continue;
    }
    return doc_marker(p) ? -1 : col(p);
  }
}

/* Nothing but a comment may follow on the line. */
static void end_of_line(yp *p)
{
  int spaced = 0;
  while(cur(p) == ' ' || cur(p) == '\t') {
    p->i++;
    spaced = 1;
  }
  if(cur(p) == '#' && (spaced || p->i == p->line_start || p->s[p->i - 1] == ' ' || p->s[p->i - 1] == '\t'))
    while(p->i < p->n && cur(p) != '\n')
      p->i++;
  if(p->i < p->n && cur(p) != '\n')
    fail(p, "unexpected text after a value");
}

static long count_nodes(const jv *v)
{
  long n = 1;
  int i;
  if(jv_kind(v) == JV_ARR || jv_kind(v) == JV_OBJ)
    for(i = 0; i < jv_len(v); i++)
      n += count_nodes(jv_kind(v) == JV_ARR ? jv_at(v, i) : jv_get(v, jv_key_at(v, i)));
  return n;
}

static void utf8_add(sb *b, unsigned long c)
{
  char t[4];
  if(c < 0x80)
    sb_ch(b, (char)c);
  else if(c < 0x800) {
    t[0] = (char)(0xC0 | (c >> 6));
    t[1] = (char)(0x80 | (c & 0x3F));
    sb_add(b, t, 2);
  }
  else if(c < 0x10000) {
    t[0] = (char)(0xE0 | (c >> 12));
    t[1] = (char)(0x80 | ((c >> 6) & 0x3F));
    t[2] = (char)(0x80 | (c & 0x3F));
    sb_add(b, t, 3);
  }
  else {
    t[0] = (char)(0xF0 | (c >> 18));
    t[1] = (char)(0x80 | ((c >> 12) & 0x3F));
    t[2] = (char)(0x80 | ((c >> 6) & 0x3F));
    t[3] = (char)(0x80 | (c & 0x3F));
    sb_add(b, t, 4);
  }
}

static void trim_blanks(sb *b)
{
  while(b->n && (b->s[b->n - 1] == ' ' || b->s[b->n - 1] == '\t'))
    b->s[--b->n] = '\0';
}

/* A line break inside quotes: one becomes a space, each further one a line feed. */
static void fold_quoted(yp *p, sb *b)
{
  int breaks = 0;
  trim_blanks(b);
  while(cur(p) == '\n' || cur(p) == ' ' || cur(p) == '\t') {
    if(cur(p) == '\n') {
      newline(p);
      breaks++;
    }
    else
      p->i++;
  }
  if(breaks == 1)
    sb_ch(b, ' ');
  else
    while(--breaks > 0)
      sb_ch(b, '\n');
}

static char *quoted(yp *p)
{
  char q = cur(p);
  sb b = { 0 };
  sb_add(&b, "", 0);
  p->i++;
  for(;;) {
    char c = cur(p);
    if(p->i >= p->n) {
      fail(p, "a quote is never closed");
      break;
    }
    if(c == q) {
      if(q == '\'' && at(p, 1) == '\'') {
        sb_ch(&b, '\'');
        p->i += 2;
        continue;
      }
      p->i++;
      return b.s;
    }
    if(c == '\n') {
      fold_quoted(p, &b);
      continue;
    }
    if(c == '\\' && q == '"') {
      char e = at(p, 1);
      int digits = 0;
      switch(e) {
      case 'n': sb_ch(&b, '\n'); break;
      case 't': case '\t': sb_ch(&b, '\t'); break;
      case 'r': sb_ch(&b, '\r'); break;
      case 'a': sb_ch(&b, '\a'); break;
      case 'b': sb_ch(&b, '\b'); break;
      case 'e': sb_ch(&b, 27); break;
      case 'f': sb_ch(&b, '\f'); break;
      case 'v': sb_ch(&b, '\v'); break;
      case ' ': case '"': case '/': case '\\': sb_ch(&b, e); break;
      case 'x': digits = 2; break;
      case 'u': digits = 4; break;
      case 'U': digits = 8; break;
      case '\n':
        p->i++;
        newline(p);
        while(cur(p) == ' ' || cur(p) == '\t')
          p->i++;
        continue;
      default:
        fail(p, "unknown escape");
        free(b.s);
        return NULL;
      }
      p->i += 2;
      if(digits) {
        unsigned long v = 0;
        int k;
        for(k = 0; k < digits; k++) {
          if(!isxdigit((unsigned char)cur(p))) {
            fail(p, "bad escape");
            free(b.s);
            return NULL;
          }
          v = v * 16 + (unsigned long)(isdigit((unsigned char)cur(p)) ? cur(p) - '0' : tolower((unsigned char)cur(p)) - 'a' + 10);
          p->i++;
        }
        if(!v || v > 0x10FFFF) {
          fail(p, "this escape cannot be held in a string");
          free(b.s);
          return NULL;
        }
        utf8_add(&b, v);
      }
      continue;
    }
    sb_ch(&b, c);
    p->i++;
  }
  free(b.s);
  return NULL;
}

static jv *plain_value(const char *t)
{
  char *end;
  if(!*t || !strcmp(t, "~") || !strcmp(t, "null") || !strcmp(t, "Null") || !strcmp(t, "NULL"))
    return jv_new(JV_NULL);
  if(!strcmp(t, "true") || !strcmp(t, "True") || !strcmp(t, "TRUE"))
    return jv_new_bool(1);
  if(!strcmp(t, "false") || !strcmp(t, "False") || !strcmp(t, "FALSE"))
    return jv_new_bool(0);
  if(t[0] == '0' && t[1] == 'x' && t[2]) {
    long v = strtol(t + 2, &end, 16);
    if(!*end)
      return jv_new_num((double)v);
  }
  if(t[0] == '0' && t[1] == 'o' && t[2]) {
    long v = strtol(t + 2, &end, 8);
    if(!*end)
      return jv_new_num((double)v);
  }
  if(isdigit((unsigned char)t[0]) || ((t[0] == '-' || t[0] == '+' || t[0] == '.') && (isdigit((unsigned char)t[1]) || t[1] == '.'))) {
    const char *c = t;
    int only_number = 1;
    for(; *c; c++)
      if(!isdigit((unsigned char)*c) && !strchr("+-.eE", *c))
        only_number = 0;
    if(only_number) {
      double v = strtod(t, &end);
      if(!*end)
        return jv_new_num(v);
    }
  }
  return jv_new_str(t);
}

/* The plain text of one line, up to a comment or the end. */
static void plain_line(yp *p, sb *b, int in_flow)
{
  size_t start = p->i, e = p->i;
  for(;;) {
    char c = cur(p);
    if(p->i >= p->n || c == '\n')
      break;
    if(c == '#' && p->i > start && (p->s[p->i - 1] == ' ' || p->s[p->i - 1] == '\t'))
      break;
    if(c == ':' && (blank_or_end(at(p, 1)) || (in_flow && strchr(",[]{}", at(p, 1)))))
      break;
    if(in_flow && strchr(",[]{}", c))
      break;
    p->i++;
    if(c != ' ' && c != '\t')
      e = p->i;
  }
  sb_add(b, p->s + start, e - start);
}

static const char *find_key_colon(const yp *p)
{
  size_t k = p->i;
  for(; k < p->n && p->s[k] != '\n'; k++) {
    if(p->s[k] == '#' && k > p->i && (p->s[k - 1] == ' ' || p->s[k - 1] == '\t'))
      return NULL;
    if(p->s[k] == ':' && (k + 1 >= p->n || blank_or_end(p->s[k + 1])))
      return p->s + k;
  }
  return NULL;
}

static jv *block_scalar(yp *p, int parent)
{
  char style = cur(p), chomp = 'c';
  int explicit_indent = 0, indent = -1, breaks = 0, first = 1, more = 0;
  sb b = { 0 };
  jv *out;
  sb_add(&b, "", 0);
  p->i++;
  while(cur(p) == '+' || cur(p) == '-' || isdigit((unsigned char)cur(p))) {
    if(isdigit((unsigned char)cur(p)))
      explicit_indent = cur(p) - '0';
    else
      chomp = cur(p);
    p->i++;
  }
  end_of_line(p);
  if(p->failed) {
    free(b.s);
    return NULL;
  }
  if(cur(p) == '\n')
    newline(p);
  if(explicit_indent)
    indent = (parent < 0 ? 0 : parent) + explicit_indent;
  while(p->i < p->n) {
    size_t begin = p->i;
    int spaces = 0, is_blank;
    size_t e;
    int this_more;
    while(p->s[begin + (size_t)spaces] == ' ' && begin + (size_t)spaces < p->n)
      spaces++;
    e = begin + (size_t)spaces;
    is_blank = e >= p->n || p->s[e] == '\n';
    if(is_blank && (indent < 0 || spaces <= indent)) {
      breaks++;
      p->i = e;
      if(p->i < p->n)
        newline(p);
      continue;
    }
    if(indent < 0) {
      if(spaces <= parent || (doc_marker(p)))
        break;
      indent = spaces;
    }
    if(spaces < indent)
      break;
    if(indent == 0 && (p->i = begin, doc_marker(p)))
      break;
    this_more = spaces > indent || (e < p->n && (p->s[e] == ' ' || p->s[e] == '\t')) || is_blank;
    if(!first) {
      if(style == '|' || more || this_more) {
        int k;
        for(k = 0; k <= breaks; k++)
          sb_ch(&b, '\n');
      }
      else if(breaks) {
        int k;
        for(k = 0; k < breaks; k++)
          sb_ch(&b, '\n');
      }
      else
        sb_ch(&b, ' ');
    }
    else {
      int k;
      for(k = 0; k < breaks; k++)
        sb_ch(&b, '\n');
    }
    first = 0;
    breaks = 0;
    more = this_more;
    p->i = begin + (size_t)indent;
    {
      size_t ls = p->i;
      while(p->i < p->n && cur(p) != '\n')
        p->i++;
      sb_add(&b, p->s + ls, p->i - ls);
    }
    if(p->i < p->n) {
      newline(p);
      breaks = 0;
      /* the break after this line is counted when the next line is written */
    }
    else if(!first) {
      breaks = -1;  /* the last line has no line feed at all */
      break;
    }
  }
  if(chomp != '-' && !first) {
    if(breaks >= 0)
      sb_ch(&b, '\n');
    if(chomp == '+') {
      int k;
      for(k = 0; k < breaks; k++)
        sb_ch(&b, '\n');
    }
  }
  out = jv_new_str(b.s);
  free(b.s);
  return out;
}

static jv *flow_node(yp *p, int depth);

static void flow_space(yp *p)
{
  for(;;) {
    while(cur(p) == ' ' || cur(p) == '\t')
      p->i++;
    if(cur(p) == '#')
      while(p->i < p->n && cur(p) != '\n')
        p->i++;
    if(cur(p) != '\n')
      return;
    newline(p);
  }
}

static jv *use_alias(yp *p);
static jv *with_props(yp *p, int parent, int depth, int in_flow);

static jv *flow_seq(yp *p, int depth)
{
  jv *arr = jv_new(JV_ARR);
  p->i++;
  for(;;) {
    jv *item;
    flow_space(p);
    if(p->i >= p->n) {
      jv_free(arr);
      return fail(p, "a [ is never closed");
    }
    if(cur(p) == ']') {
      p->i++;
      return arr;
    }
    item = flow_node(p, depth + 1);
    if(!item) {
      jv_free(arr);
      return NULL;
    }
    jv_push(arr, item);
    flow_space(p);
    if(cur(p) == ',')
      p->i++;
    else if(cur(p) != ']') {
      jv_free(arr);
      return fail(p, p->i >= p->n ? "a [ is never closed" : "expected , or ]");
    }
  }
}

static jv *flow_map(yp *p, int depth)
{
  jv *obj = jv_new(JV_OBJ);
  p->i++;
  for(;;) {
    char *key;
    jv *val;
    int was_quoted;
    flow_space(p);
    if(p->i >= p->n) {
      jv_free(obj);
      return fail(p, "a { is never closed");
    }
    if(cur(p) == '}') {
      p->i++;
      return obj;
    }
    was_quoted = cur(p) == '"' || cur(p) == '\'';
    if(was_quoted)
      key = quoted(p);
    else {
      sb b = { 0 };
      sb_add(&b, "", 0);
      plain_line(p, &b, 1);
      key = b.s;
    }
    if(!key) {
      jv_free(obj);
      return NULL;
    }
    flow_space(p);
    if(cur(p) == ':') {
      p->i++;
      flow_space(p);
      if(cur(p) == ',' || cur(p) == '}')
        val = jv_new(JV_NULL);
      else
        val = flow_node(p, depth + 1);
    }
    else
      val = jv_new(JV_NULL);
    if(!val) {
      free(key);
      jv_free(obj);
      return NULL;
    }
    jv_set(obj, key, val);
    free(key);
    flow_space(p);
    if(cur(p) == ',')
      p->i++;
    else if(cur(p) != '}') {
      jv_free(obj);
      return fail(p, p->i >= p->n ? "a { is never closed" : "expected , or }");
    }
  }
}

static jv *flow_node(yp *p, int depth)
{
  if(depth > YAML_MAX_DEPTH)
    return fail(p, "nested too deep");
  switch(cur(p)) {
  case '[':
    return flow_seq(p, depth);
  case '{':
    return flow_map(p, depth);
  case '&': case '!': case '*':
    return with_props(p, -1, depth, 1);
  case '"': case '\'': {
    char *t = quoted(p);
    jv *v = t ? jv_new_str(t) : NULL;
    free(t);
    return v;
  }
  default: {
    sb b = { 0 };
    jv *v;
    sb_add(&b, "", 0);
    plain_line(p, &b, 1);
    if(!b.n) {
      free(b.s);
      return fail(p, "expected a value");
    }
    v = plain_value(b.s);
    free(b.s);
    return v;
  }
  }
}

static jv *use_alias(yp *p)
{
  size_t s;
  int k;
  p->i++;
  s = p->i;
  while(p->i < p->n && !blank_or_end(cur(p)) && !strchr(",[]{}", cur(p)))
    p->i++;
  for(k = p->nanchors - 1; k >= 0; k--) {
    if(strlen(p->anchors[k].name) == p->i - s && !memcmp(p->anchors[k].name, p->s + s, p->i - s)) {
      if(p->anchors[k].nodes > p->budget)
        return fail(p, "this alias expands to too much");
      p->budget -= p->anchors[k].nodes;
      return jv_clone(p->anchors[k].value);
    }
  }
  return fail(p, "this alias has no anchor");
}

static jv *block_node(yp *p, int parent, int depth);
static jv *block_map(yp *p, int c, int depth);
static jv *block_seq(yp *p, int c, int depth);

static void remember(yp *p, const char *name, size_t n, jv *v)
{
  anchor *g = realloc(p->anchors, sizeof(anchor) * (size_t)(p->nanchors + 1));
  char *copy = malloc(n + 1);
  jv *value = jv_clone(v);
  if(!g || !copy || !value) {
    if(g)
      p->anchors = g;
    free(copy);
    jv_free(value);
    return;
  }
  p->anchors = g;
  memcpy(copy, name, n);
  copy[n] = '\0';
  p->anchors[p->nanchors].name = copy;
  p->anchors[p->nanchors].value = value;
  p->anchors[p->nanchors].nodes = count_nodes(v);
  p->nanchors++;
}

/* A value with &anchor and !tag in front of it, or an alias. */
static jv *with_props(yp *p, int parent, int depth, int in_flow)
{
  const char *anchor_name = NULL;
  size_t anchor_len = 0;
  int force_str = 0;
  jv *v;
  while(cur(p) == '&' || cur(p) == '!') {
    if(cur(p) == '&') {
      p->i++;
      anchor_name = p->s + p->i;
      while(p->i < p->n && !blank_or_end(cur(p)) && !(in_flow && strchr(",[]{}", cur(p))))
        p->i++;
      anchor_len = (size_t)(p->s + p->i - anchor_name);
    }
    else {
      static const char *const core[] = { "!!map", "!!seq", "!!str", "!!int", "!!float", "!!bool", "!!null", "!!binary", NULL };
      size_t s = p->i;
      int k, known = 0;
      while(p->i < p->n && !blank_or_end(cur(p)) && !(in_flow && strchr(",[]{}", cur(p))))
        p->i++;
      for(k = 0; core[k]; k++)
        if(strlen(core[k]) == p->i - s && !memcmp(core[k], p->s + s, p->i - s))
          known = 1;
      if(!known) {
        p->i = s;
        return fail(p, "this tag is not supported");
      }
      if(p->i - s == 5 && !memcmp(p->s + s, "!!str", 5))
        force_str = 1;
    }
    while(cur(p) == ' ')
      p->i++;
  }
  if(force_str && cur(p) != '"' && cur(p) != '\'' && cur(p) != '|' && cur(p) != '>') {
    sb b = { 0 };
    sb_add(&b, "", 0);
    plain_line(p, &b, in_flow);
    v = jv_new_str(b.s);
    free(b.s);
  }
  else if(cur(p) == '*')
    v = use_alias(p);
  else if(in_flow)
    v = flow_node(p, depth);
  else
    v = block_node(p, parent, depth);
  if(v && anchor_name)
    remember(p, anchor_name, anchor_len, v);
  return v;
}

/* A value beginning where the cursor is. parent: the column it must be deeper than. */
static jv *block_node(yp *p, int parent, int depth)
{
  char c;
  int here, after_key = p->after_key;
  p->after_key = 0;
  while(cur(p) == ' ')
    p->i++;
  c = cur(p);
  /* nothing on this line: it may be on the next ones */
  if(p->i >= p->n || c == '\n' || c == '#') {
    int nc = next_content(p);
    if(p->failed)
      return NULL;
    after_key = 0;
    if(nc < 0 || nc <= parent) {
      /* a list may sit at the indent of its key */
      if(nc >= 0 && nc == parent && parent >= 0 && cur(p) == '-' && blank_or_end(at(p, 1)))
        return block_seq(p, nc, depth);
      return jv_new(JV_NULL);
    }
    c = cur(p);
  }
  here = col(p);
  switch(c) {
  case '&': case '!': case '*':
    p->after_key = after_key;
    return with_props(p, parent, depth, 0);
  case '|': case '>':
    return block_scalar(p, parent);
  case '[': case '{': {
    jv *v = flow_node(p, depth);
    if(v) {
      end_of_line(p);
      if(p->failed) {
        jv_free(v);
        return NULL;
      }
    }
    return v;
  }
  case '"': case '\'': {
    size_t s = p->i;
    int sl = p->line;
    size_t sstart = p->line_start;
    char *t = quoted(p);
    size_t k = p->i;
    jv *v;
    if(!t)
      return NULL;
    while(k < p->n && (p->s[k] == ' ' || p->s[k] == '\t'))
      k++;
    if(k < p->n && p->s[k] == ':' && (k + 1 >= p->n || blank_or_end(p->s[k + 1])) && p->line == sl) {
      free(t);
      if(after_key)
        return fail(p, "a value cannot hold \": \" unless quoted");
      p->i = s;
      p->line = sl;
      p->line_start = sstart;
      return block_map(p, here, depth);
    }
    v = jv_new_str(t);
    free(t);
    end_of_line(p);
    if(p->failed) {
      jv_free(v);
      return NULL;
    }
    return v;
  }
  case '-':
    if(blank_or_end(at(p, 1)))
      return block_seq(p, here, depth);
    break;
  case '?':
    if(blank_or_end(at(p, 1)))
      return fail(p, "complex keys are not supported");
    break;
  case '@': case '`': case '%': case ',': case ']': case '}':
    return fail(p, "a plain value cannot begin with %c", c);
  default:
    break;
  }
  if(find_key_colon(p)) {
    if(after_key)
      return fail(p, "a value cannot hold \": \" unless quoted");
    return block_map(p, here, depth);
  }
  /* a plain scalar, over as many lines as stay deeper than its parent */
  {
    sb b = { 0 };
    jv *v;
    sb_add(&b, "", 0);
    plain_line(p, &b, 0);
    if(cur(p) == ':')
      fail(p, "a value cannot hold \": \" unless quoted");
    while(!p->failed) {
      size_t save_i = p->i, save_ls = p->line_start;
      int save_line = p->line, breaks = 0;
      if(cur(p) != '\n')
        break;
      while(cur(p) == '\n') {
        newline(p);
        breaks++;
        while(cur(p) == ' ')
          p->i++;
      }
      if(p->i >= p->n || cur(p) == '#' || col(p) <= parent || doc_marker(p)) {
        p->i = save_i;
        p->line = save_line;
        p->line_start = save_ls;
        break;
      }
      if(breaks == 1)
        sb_ch(&b, ' ');
      else
        while(--breaks > 0)
          sb_ch(&b, '\n');
      plain_line(p, &b, 0);
      if(cur(p) == ':')
        fail(p, "a value cannot hold \": \" unless quoted");
    }
    if(p->failed) {
      free(b.s);
      return NULL;
    }
    end_of_line(p);
    if(p->failed) {
      free(b.s);
      return NULL;
    }
    v = plain_value(b.s);
    free(b.s);
    return v;
  }
}

static void merge_into(jv *obj, const jv *from)
{
  int i;
  if(jv_kind(from) != JV_OBJ)
    return;
  for(i = 0; i < jv_len(from); i++) {
    const char *k = jv_key_at(from, i);
    if(!jv_get(obj, k))
      jv_set(obj, k, jv_clone(jv_get(from, k)));
  }
}

static jv *block_map(yp *p, int c, int depth)
{
  jv *obj, *merges;
  int i;
  if(depth > YAML_MAX_DEPTH)
    return fail(p, "nested too deep");
  obj = jv_new(JV_OBJ);
  merges = jv_new(JV_ARR);
  for(;;) {
    char *key = NULL;
    jv *val;
    int key_quoted = cur(p) == '"' || cur(p) == '\'';
    if(key_quoted) {
      key = quoted(p);
      if(key) {
        while(cur(p) == ' ' || cur(p) == '\t')
          p->i++;
        if(cur(p) != ':') {
          free(key);
          key = NULL;
          fail(p, "expected : after a key");
        }
      }
    }
    else {
      const char *colon = find_key_colon(p);
      if(cur(p) == '?' && blank_or_end(at(p, 1)))
        fail(p, "complex keys are not supported");
      else if(!colon)
        fail(p, "expected a key");
      else {
        size_t len = (size_t)(colon - (p->s + p->i));
        while(len && (p->s[p->i + len - 1] == ' ' || p->s[p->i + len - 1] == '\t'))
          len--;
        key = malloc(len + 1);
        if(key) {
          memcpy(key, p->s + p->i, len);
          key[len] = '\0';
        }
        p->i = (size_t)(colon - p->s);
      }
    }
    if(!key)
      goto bad;
    p->i++;  /* : */
    p->after_key = 1;
    val = block_node(p, c, depth + 1);
    if(!val) {
      free(key);
      goto bad;
    }
    if(!key_quoted && !strcmp(key, "<<")) {
      if(jv_kind(val) == JV_ARR)
        for(i = 0; i < jv_len(val); i++)
          jv_push(merges, jv_clone(jv_at(val, i)));
      else
        jv_push(merges, jv_clone(val));
      jv_free(val);
    }
    else
      jv_set(obj, key, val);
    free(key);
    {
      int nc = next_content(p);
      if(p->failed)
        goto bad;
      if(nc < 0 || nc < c)
        break;
      if(nc > c) {
        fail(p, "this does not line up with what it belongs to");
        goto bad;
      }
      if(cur(p) == '-' && blank_or_end(at(p, 1)))
        break;
    }
  }
  for(i = 0; i < jv_len(merges); i++)
    merge_into(obj, jv_at(merges, i));
  jv_free(merges);
  return obj;
bad:
  jv_free(obj);
  jv_free(merges);
  return NULL;
}

static jv *block_seq(yp *p, int c, int depth)
{
  jv *arr;
  if(depth > YAML_MAX_DEPTH)
    return fail(p, "nested too deep");
  arr = jv_new(JV_ARR);
  for(;;) {
    jv *item;
    p->i++;  /* - */
    item = block_node(p, c, depth + 1);
    if(!item) {
      jv_free(arr);
      return NULL;
    }
    jv_push(arr, item);
    {
      int nc = next_content(p);
      if(p->failed) {
        jv_free(arr);
        return NULL;
      }
      if(nc < 0 || nc < c)
        return arr;
      if(nc > c) {
        jv_free(arr);
        return fail(p, "this does not line up with what it belongs to");
      }
      if(!(cur(p) == '-' && blank_or_end(at(p, 1))))
        return arr;
    }
  }
}

jv *yaml_parse(const char *text, size_t len, char *err, size_t errn)
{
  yp p;
  jv *v = NULL;
  char *clean;
  size_t i, w = 0;
  int k;

  if(err && errn)
    err[0] = '\0';
  /* JSON first: the common case for large files, and faster */
  for(i = 0; i < len && strchr(" \t\r\n", text[i]); i++)
    ;
  if(i < len && (text[i] == '{' || text[i] == '[')) {
    v = jv_parse(text, len, NULL, 0);
    if(v)
      return v;
  }
  clean = malloc(len + 1);
  if(!clean) {
    if(err && errn)
      snprintf(err, errn, "out of memory");
    return NULL;
  }
  i = len >= 3 && !memcmp(text, "\xEF\xBB\xBF", 3) ? 3 : 0;
  for(; i < len; i++)
    if(!(text[i] == '\r' && i + 1 < len && text[i + 1] == '\n'))
      clean[w++] = text[i];
  clean[w] = '\0';

  memset(&p, 0, sizeof(p));
  p.s = clean;
  p.n = w;
  p.line = 1;
  p.err = err;
  p.errn = errn;
  p.budget = NODE_BUDGET;

  /* directives and the start of the document */
  while(cur(&p) == '%') {
    while(p.i < p.n && cur(&p) != '\n')
      p.i++;
    if(cur(&p) == '\n')
      newline(&p);
  }
  k = next_content(&p);
  if(k < 0 && at(&p, 0) == '-' && !p.failed) {
    p.i += 3;
    v = block_node(&p, -1, 1);
  }
  else if(k < 0 && !p.failed && p.i < p.n)
    v = jv_new(JV_NULL);  /* only ... */
  else if(k < 0 && !p.failed)
    v = jv_new(JV_NULL);  /* nothing at all */
  else if(!p.failed)
    v = block_node(&p, -1, 1);

  if(v && !p.failed) {
    int nc = next_content(&p);
    if(p.failed) {
      jv_free(v);
      v = NULL;
    }
    else if(nc >= 0) {
      fail(&p, "unexpected text after the document");
      jv_free(v);
      v = NULL;
    }
    else if(at(&p, 0) == '-' && p.i < p.n) {
      fail(&p, "a file with several documents is not supported");
      jv_free(v);
      v = NULL;
    }
  }
  if(p.failed && v) {
    jv_free(v);
    v = NULL;
  }
  for(k = 0; k < p.nanchors; k++) {
    free(p.anchors[k].name);
    jv_free(p.anchors[k].value);
  }
  free(p.anchors);
  free(clean);
  return v;
}
