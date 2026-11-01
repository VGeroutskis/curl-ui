#include "json_fmt.h"

#include <stdlib.h>
#include <string.h>

#define INLINE_MAX_WIDTH 60

typedef struct {
  const char *s, *end;
  char *out;
  size_t n, cap;
  int failed;
} jctx;

static void emit(jctx *j, const char *p, size_t len)
{
  if(j->failed)
    return;
  if(j->n + len + 1 > j->cap) {
    size_t cap = j->cap ? j->cap * 2 : 4096;
    char *grown;
    while(cap < j->n + len + 1)
      cap *= 2;
    grown = realloc(j->out, cap);
    if(!grown) {
      j->failed = 1;
      return;
    }
    j->out = grown;
    j->cap = cap;
  }
  memcpy(j->out + j->n, p, len);
  j->n += len;
}

static void emit_c(jctx *j, char c) { emit(j, &c, 1); }

static void indent(jctx *j, int depth)
{
  int i;
  emit_c(j, '\n');
  for(i = 0; i < depth; i++)
    emit(j, "  ", 2);
}

static void skip_ws(const char **ps, const char *end)
{
  const char *s = *ps;
  while(s < end && (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r'))
    s++;
  *ps = s;
}

static int is_hex(char c)
{
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

/* Returns the character after the closing quote, or NULL. */
static const char *scan_string(const char *s, const char *end)
{
  if(s >= end || *s != '"')
    return NULL;
  s++;
  while(s < end) {
    unsigned char c = (unsigned char)*s;
    if(c == '"')
      return s + 1;
    if(c < 0x20)
      return NULL;
    if(c == '\\') {
      s++;
      if(s >= end)
        return NULL;
      if(*s == 'u') {
        int i;
        if(end - s < 5)
          return NULL;
        for(i = 1; i <= 4; i++)
          if(!is_hex(s[i]))
            return NULL;
        s += 4;
      }
      else if(!strchr("\"\\/bfnrt", *s) || !*s)
        return NULL;
    }
    s++;
  }
  return NULL;
}

static const char *scan_number(const char *s, const char *end)
{
  const char *start;
  if(s < end && *s == '-')
    s++;
  if(s >= end)
    return NULL;
  if(*s == '0')
    s++;
  else if(*s >= '1' && *s <= '9') {
    while(s < end && *s >= '0' && *s <= '9')
      s++;
  }
  else
    return NULL;
  if(s < end && *s == '.') {
    start = ++s;
    while(s < end && *s >= '0' && *s <= '9')
      s++;
    if(s == start)
      return NULL;
  }
  if(s < end && (*s == 'e' || *s == 'E')) {
    s++;
    if(s < end && (*s == '+' || *s == '-'))
      s++;
    start = s;
    while(s < end && *s >= '0' && *s <= '9')
      s++;
    if(s == start)
      return NULL;
  }
  return s;
}

static const char *scan_word(const char *s, const char *end, const char *word)
{
  size_t n = strlen(word);
  if((size_t)(end - s) >= n && !memcmp(s, word, n))
    return s + n;
  return NULL;
}

static const char *scan_scalar(const char *s, const char *end)
{
  if(s >= end)
    return NULL;
  switch(*s) {
  case '"':
    return scan_string(s, end);
  case 't':
    return scan_word(s, end, "true");
  case 'f':
    return scan_word(s, end, "false");
  case 'n':
    return scan_word(s, end, "null");
  default:
    return scan_number(s, end);
  }
}

/* s is just past '['. If the array is scalars only and short, print it inline
   and return 1. Invalid or long input returns 0; the full parser judges it. */
static int try_inline_array(jctx *j)
{
  const char *s = j->s;
  size_t width = 0;
  int count = 0;
  skip_ws(&s, j->end);
  for(;;) {
    const char *e = scan_scalar(s, j->end);
    if(!e)
      return 0;
    width += (size_t)(e - s) + 2;
    if(width > INLINE_MAX_WIDTH)
      return 0;
    count++;
    s = e;
    skip_ws(&s, j->end);
    if(s >= j->end)
      return 0;
    if(*s == ']')
      break;
    if(*s != ',')
      return 0;
    s++;
    skip_ws(&s, j->end);
  }
  (void)count;
  /* It is valid and fits: print it. */
  s = j->s;
  skip_ws(&s, j->end);
  emit_c(j, '[');
  for(;;) {
    const char *e = scan_scalar(s, j->end);
    emit(j, s, (size_t)(e - s));
    s = e;
    skip_ws(&s, j->end);
    if(*s == ']')
      break;
    emit(j, ", ", 2);
    s++;
    skip_ws(&s, j->end);
  }
  emit_c(j, ']');
  j->s = s + 1;
  return 1;
}

static int parse_value(jctx *j, int depth)
{
  const char *e;
  skip_ws(&j->s, j->end);
  if(j->s >= j->end || depth > JSON_MAX_DEPTH)
    return 0;
  if(*j->s == '{' || *j->s == '[') {
    int is_obj = *j->s == '{';
    char close = is_obj ? '}' : ']';
    int first = 1;
    if(depth == JSON_MAX_DEPTH)
      return 0;
    j->s++;
    skip_ws(&j->s, j->end);
    if(j->s < j->end && *j->s == close) {
      j->s++;
      emit(j, is_obj ? "{}" : "[]", 2);
      return 1;
    }
    if(!is_obj && try_inline_array(j))
      return 1;
    emit_c(j, is_obj ? '{' : '[');
    for(;;) {
      indent(j, depth + 1);
      if(is_obj) {
        skip_ws(&j->s, j->end);
        e = scan_string(j->s, j->end);
        if(!e)
          return 0;
        emit(j, j->s, (size_t)(e - j->s));
        j->s = e;
        skip_ws(&j->s, j->end);
        if(j->s >= j->end || *j->s != ':')
          return 0;
        j->s++;
        emit(j, ": ", 2);
      }
      if(!parse_value(j, depth + 1))
        return 0;
      skip_ws(&j->s, j->end);
      if(j->s >= j->end)
        return 0;
      if(*j->s == close) {
        j->s++;
        break;
      }
      if(*j->s != ',')
        return 0;
      j->s++;
      emit_c(j, ',');
      first = 0;
    }
    (void)first;
    indent(j, depth);
    emit_c(j, close);
    return 1;
  }
  e = scan_scalar(j->s, j->end);
  if(!e)
    return 0;
  emit(j, j->s, (size_t)(e - j->s));
  j->s = e;
  return 1;
}

char *json_pretty(const char *text, size_t len, size_t *out_len)
{
  jctx j;
  memset(&j, 0, sizeof(j));
  j.s = text;
  j.end = text + len;
  if(!parse_value(&j, 0)) {
    free(j.out);
    return NULL;
  }
  skip_ws(&j.s, j.end);
  if(j.s != j.end || j.failed) {
    free(j.out);
    return NULL;
  }
  emit_c(&j, '\0');
  if(j.failed) {
    free(j.out);
    return NULL;
  }
  if(out_len)
    *out_len = j.n - 1;
  return j.out;
}
