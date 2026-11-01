#include "json.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct jv {
  jv_type t;
  int b;
  double n;
  char *s;
  jv **items;
  char **keys;
  int count, cap;
};

jv *jv_new(jv_type t)
{
  jv *v = calloc(1, sizeof(*v));
  if(v)
    v->t = t;
  return v;
}

static char *dup_str(const char *s)
{
  size_t n = strlen(s) + 1;
  char *d = malloc(n);
  if(d)
    memcpy(d, s, n);
  return d;
}

jv *jv_new_str(const char *s)
{
  jv *v = jv_new(JV_STR);
  if(v) {
    v->s = dup_str(s ? s : "");
    if(!v->s) {
      free(v);
      return NULL;
    }
  }
  return v;
}

jv *jv_new_num(double n)
{
  jv *v = jv_new(JV_NUM);
  if(v)
    v->n = n;
  return v;
}

jv *jv_new_bool(int b)
{
  jv *v = jv_new(JV_BOOL);
  if(v)
    v->b = !!b;
  return v;
}

void jv_free(jv *v)
{
  int i;
  if(!v)
    return;
  for(i = 0; i < v->count; i++) {
    jv_free(v->items[i]);
    if(v->keys)
      free(v->keys[i]);
  }
  free(v->items);
  free(v->keys);
  free(v->s);
  free(v);
}

jv_type jv_kind(const jv *v)
{
  return v ? v->t : JV_NULL;
}

const char *jv_str(const jv *v)
{
  return v && v->t == JV_STR ? v->s : NULL;
}

double jv_num(const jv *v)
{
  return v && v->t == JV_NUM ? v->n : 0;
}

int jv_bool(const jv *v)
{
  return v && v->t == JV_BOOL ? v->b : 0;
}

int jv_len(const jv *v)
{
  return v && (v->t == JV_ARR || v->t == JV_OBJ) ? v->count : 0;
}

jv *jv_at(const jv *v, int i)
{
  return i >= 0 && i < jv_len(v) ? v->items[i] : NULL;
}

jv *jv_get(const jv *obj, const char *key)
{
  int i;
  if(jv_kind(obj) != JV_OBJ)
    return NULL;
  for(i = 0; i < obj->count; i++)
    if(!strcmp(obj->keys[i], key))
      return obj->items[i];
  return NULL;
}

const char *jv_key_at(const jv *obj, int i)
{
  return jv_kind(obj) == JV_OBJ && i >= 0 && i < obj->count ? obj->keys[i] : NULL;
}

static int grow(jv *v)
{
  if(v->count == v->cap) {
    int cap = v->cap ? v->cap * 2 : 8;
    jv **items = realloc(v->items, (size_t)cap * sizeof(*items));
    if(!items)
      return -1;
    v->items = items;
    if(v->t == JV_OBJ) {
      char **keys = realloc(v->keys, (size_t)cap * sizeof(*keys));
      if(!keys)
        return -1;
      v->keys = keys;
    }
    v->cap = cap;
  }
  return 0;
}

void jv_push(jv *arr, jv *val)
{
  if(jv_kind(arr) != JV_ARR || grow(arr)) {
    jv_free(val);
    return;
  }
  arr->items[arr->count++] = val;
}

void jv_set(jv *obj, const char *key, jv *val)
{
  int i;
  char *k;
  if(jv_kind(obj) != JV_OBJ) {
    jv_free(val);
    return;
  }
  for(i = 0; i < obj->count; i++) {
    if(!strcmp(obj->keys[i], key)) {
      jv_free(obj->items[i]);
      obj->items[i] = val;
      return;
    }
  }
  k = dup_str(key);
  if(!k || grow(obj)) {
    free(k);
    jv_free(val);
    return;
  }
  obj->keys[obj->count] = k;
  obj->items[obj->count++] = val;
}

jv *jv_clone(const jv *v)
{
  jv *c;
  int i;
  if(!v)
    return NULL;
  switch(v->t) {
  case JV_STR:
    return jv_new_str(v->s);
  case JV_NUM:
    return jv_new_num(v->n);
  case JV_BOOL:
    return jv_new_bool(v->b);
  case JV_NULL:
    return jv_new(JV_NULL);
  default:
    break;
  }
  c = jv_new(v->t);
  for(i = 0; c && i < v->count; i++) {
    if(v->t == JV_OBJ)
      jv_set(c, v->keys[i], jv_clone(v->items[i]));
    else
      jv_push(c, jv_clone(v->items[i]));
  }
  return c;
}

/* ---- parser ---- */

typedef struct {
  const char *s;
  size_t n, i;
  char *err;
  size_t errlen;
  int failed;
} parser;

static void fail(parser *p, const char *msg)
{
  size_t k;
  int line = 1, col = 1;
  if(p->failed)
    return;
  p->failed = 1;
  for(k = 0; k < p->i && k < p->n; k++) {
    if(p->s[k] == '\n') {
      line++;
      col = 1;
    }
    else
      col++;
  }
  if(p->err && p->errlen)
    snprintf(p->err, p->errlen, "line %d column %d: %s", line, col, msg);
}

static void skip_ws(parser *p)
{
  while(p->i < p->n && (p->s[p->i] == ' ' || p->s[p->i] == '\t' || p->s[p->i] == '\n' || p->s[p->i] == '\r'))
    p->i++;
}

static int hex4(parser *p, unsigned *out)
{
  unsigned v = 0;
  int k;
  if(p->n - p->i < 4)
    return -1;
  for(k = 0; k < 4; k++) {
    char c = p->s[p->i + (size_t)k];
    v <<= 4;
    if(c >= '0' && c <= '9')
      v |= (unsigned)(c - '0');
    else if(c >= 'a' && c <= 'f')
      v |= (unsigned)(c - 'a' + 10);
    else if(c >= 'A' && c <= 'F')
      v |= (unsigned)(c - 'A' + 10);
    else
      return -1;
  }
  p->i += 4;
  *out = v;
  return 0;
}

static size_t put_utf8(char *d, unsigned c)
{
  if(c < 0x80) {
    d[0] = (char)c;
    return 1;
  }
  if(c < 0x800) {
    d[0] = (char)(0xC0 | (c >> 6));
    d[1] = (char)(0x80 | (c & 0x3F));
    return 2;
  }
  if(c < 0x10000) {
    d[0] = (char)(0xE0 | (c >> 12));
    d[1] = (char)(0x80 | ((c >> 6) & 0x3F));
    d[2] = (char)(0x80 | (c & 0x3F));
    return 3;
  }
  d[0] = (char)(0xF0 | (c >> 18));
  d[1] = (char)(0x80 | ((c >> 12) & 0x3F));
  d[2] = (char)(0x80 | ((c >> 6) & 0x3F));
  d[3] = (char)(0x80 | (c & 0x3F));
  return 4;
}

/* At the opening quote. Decoded text is never longer than the source. */
static char *parse_string(parser *p)
{
  size_t start = ++p->i, end = start, out = 0;
  char *d;
  while(end < p->n && p->s[end] != '"') {
    if(p->s[end] == '\\')
      end++;
    end++;
  }
  if(end >= p->n) {
    fail(p, "unterminated string");
    return NULL;
  }
  d = malloc(end - start + 1);
  if(!d) {
    fail(p, "out of memory");
    return NULL;
  }
  while(p->s[p->i] != '"') {
    unsigned char c = (unsigned char)p->s[p->i];
    if(c < 0x20) {
      fail(p, "control character in string");
      free(d);
      return NULL;
    }
    if(c != '\\') {
      d[out++] = (char)c;
      p->i++;
      continue;
    }
    p->i++;
    c = (unsigned char)p->s[p->i++];
    switch(c) {
    case '"': d[out++] = '"'; break;
    case '\\': d[out++] = '\\'; break;
    case '/': d[out++] = '/'; break;
    case 'b': d[out++] = '\b'; break;
    case 'f': d[out++] = '\f'; break;
    case 'n': d[out++] = '\n'; break;
    case 'r': d[out++] = '\r'; break;
    case 't': d[out++] = '\t'; break;
    case 'u': {
      unsigned u, lo;
      if(hex4(p, &u))
        goto bad_escape;
      if(u >= 0xD800 && u <= 0xDBFF) {
        if(p->n - p->i < 6 || p->s[p->i] != '\\' || p->s[p->i + 1] != 'u')
          goto bad_escape;
        p->i += 2;
        if(hex4(p, &lo) || lo < 0xDC00 || lo > 0xDFFF)
          goto bad_escape;
        u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
      }
      else if((u >= 0xDC00 && u <= 0xDFFF) || u == 0)
        goto bad_escape;
      out += put_utf8(d + out, u);
      break;
    }
    default:
      goto bad_escape;
    }
  }
  p->i++;
  d[out] = '\0';
  return d;
bad_escape:
  fail(p, "bad escape in string");
  free(d);
  return NULL;
}

static jv *parse_number(parser *p)
{
  size_t i = p->i, digits;
  char buf[64];
  size_t len;
  if(i < p->n && p->s[i] == '-')
    i++;
  digits = i;
  while(i < p->n && p->s[i] >= '0' && p->s[i] <= '9')
    i++;
  if(i == digits || (p->s[digits] == '0' && i - digits > 1)) {
    fail(p, "bad number");
    return NULL;
  }
  if(i < p->n && p->s[i] == '.') {
    digits = ++i;
    while(i < p->n && p->s[i] >= '0' && p->s[i] <= '9')
      i++;
    if(i == digits) {
      fail(p, "bad number");
      return NULL;
    }
  }
  if(i < p->n && (p->s[i] == 'e' || p->s[i] == 'E')) {
    i++;
    if(i < p->n && (p->s[i] == '+' || p->s[i] == '-'))
      i++;
    digits = i;
    while(i < p->n && p->s[i] >= '0' && p->s[i] <= '9')
      i++;
    if(i == digits) {
      fail(p, "bad number");
      return NULL;
    }
  }
  len = i - p->i;
  if(len >= sizeof(buf)) {
    fail(p, "number too long");
    return NULL;
  }
  memcpy(buf, p->s + p->i, len);
  buf[len] = '\0';
  p->i = i;
  return jv_new_num(strtod(buf, NULL));
}

static int literal(parser *p, const char *word)
{
  size_t n = strlen(word);
  if(p->n - p->i >= n && !memcmp(p->s + p->i, word, n)) {
    p->i += n;
    return 1;
  }
  return 0;
}

static jv *parse_value(parser *p, int depth);

static jv *parse_container(parser *p, int depth, jv_type t)
{
  char close = t == JV_ARR ? ']' : '}';
  jv *v;
  if(depth >= JV_MAX_DEPTH) {
    fail(p, "nested too deep");
    return NULL;
  }
  v = jv_new(t);
  p->i++;
  skip_ws(p);
  if(p->i < p->n && p->s[p->i] == close) {
    p->i++;
    return v;
  }
  for(;;) {
    char *key = NULL;
    jv *item;
    skip_ws(p);
    if(t == JV_OBJ) {
      if(p->i >= p->n || p->s[p->i] != '"') {
        fail(p, "expected a string key");
        jv_free(v);
        return NULL;
      }
      key = parse_string(p);
      if(!key) {
        jv_free(v);
        return NULL;
      }
      skip_ws(p);
      if(p->i >= p->n || p->s[p->i] != ':') {
        fail(p, "expected ':'");
        free(key);
        jv_free(v);
        return NULL;
      }
      p->i++;
    }
    item = parse_value(p, depth + 1);
    if(!item) {
      free(key);
      jv_free(v);
      return NULL;
    }
    if(t == JV_OBJ)
      jv_set(v, key, item);
    else
      jv_push(v, item);
    free(key);
    skip_ws(p);
    if(p->i < p->n && p->s[p->i] == ',') {
      p->i++;
      continue;
    }
    if(p->i < p->n && p->s[p->i] == close) {
      p->i++;
      return v;
    }
    fail(p, t == JV_ARR ? "expected ',' or ']'" : "expected ',' or '}'");
    jv_free(v);
    return NULL;
  }
}

static jv *parse_value(parser *p, int depth)
{
  char c;
  skip_ws(p);
  if(p->i >= p->n) {
    fail(p, "unexpected end of input");
    return NULL;
  }
  c = p->s[p->i];
  if(c == '{')
    return parse_container(p, depth, JV_OBJ);
  if(c == '[')
    return parse_container(p, depth, JV_ARR);
  if(c == '"') {
    char *s = parse_string(p);
    jv *v;
    if(!s)
      return NULL;
    v = jv_new(JV_STR);
    if(v)
      v->s = s;
    else
      free(s);
    return v;
  }
  if(c == '-' || (c >= '0' && c <= '9'))
    return parse_number(p);
  if(literal(p, "true"))
    return jv_new_bool(1);
  if(literal(p, "false"))
    return jv_new_bool(0);
  if(literal(p, "null"))
    return jv_new(JV_NULL);
  fail(p, "unexpected character");
  return NULL;
}

jv *jv_parse(const char *text, size_t len, char *err, size_t errlen)
{
  parser p;
  jv *v;
  p.s = text;
  p.n = len;
  p.i = 0;
  p.err = err;
  p.errlen = errlen;
  p.failed = 0;
  v = parse_value(&p, 0);
  if(v) {
    skip_ws(&p);
    if(p.i != p.n) {
      fail(&p, "text after the value");
      jv_free(v);
      return NULL;
    }
  }
  return v;
}

/* ---- writer ---- */

typedef struct {
  char *d;
  size_t n, cap;
  int oom;
} sbuf;

static void sb_add(sbuf *b, const char *s, size_t n)
{
  if(b->oom)
    return;
  if(b->n + n + 1 > b->cap) {
    size_t cap = b->cap ? b->cap : 256;
    char *d;
    while(cap < b->n + n + 1)
      cap *= 2;
    d = realloc(b->d, cap);
    if(!d) {
      b->oom = 1;
      return;
    }
    b->d = d;
    b->cap = cap;
  }
  memcpy(b->d + b->n, s, n);
  b->n += n;
  b->d[b->n] = '\0';
}

static void sb_str(sbuf *b, const char *s)
{
  sb_add(b, s, strlen(s));
}

static void write_string(sbuf *b, const char *s)
{
  sb_add(b, "\"", 1);
  for(; *s; s++) {
    unsigned char c = (unsigned char)*s;
    if(c == '"')
      sb_add(b, "\\\"", 2);
    else if(c == '\\')
      sb_add(b, "\\\\", 2);
    else if(c == '\n')
      sb_add(b, "\\n", 2);
    else if(c == '\r')
      sb_add(b, "\\r", 2);
    else if(c == '\t')
      sb_add(b, "\\t", 2);
    else if(c < 0x20) {
      char tmp[8];
      snprintf(tmp, sizeof(tmp), "\\u%04x", c);
      sb_str(b, tmp);
    }
    else
      sb_add(b, s, 1);
  }
  sb_add(b, "\"", 1);
}

static void indent(sbuf *b, int pretty, int depth)
{
  int i;
  if(!pretty)
    return;
  sb_add(b, "\n", 1);
  for(i = 0; i < depth; i++)
    sb_add(b, "  ", 2);
}

static void write_value(sbuf *b, const jv *v, int pretty, int depth)
{
  int i;
  switch(jv_kind(v)) {
  case JV_NULL:
    sb_str(b, "null");
    break;
  case JV_BOOL:
    sb_str(b, v->b ? "true" : "false");
    break;
  case JV_NUM: {
    char tmp[40];
    if(isnan(v->n) || isinf(v->n))
      sb_str(b, "null");
    else {
      snprintf(tmp, sizeof(tmp), "%.15g", v->n);
      if(strtod(tmp, NULL) != v->n)
        snprintf(tmp, sizeof(tmp), "%.17g", v->n);
      sb_str(b, tmp);
    }
    break;
  }
  case JV_STR:
    write_string(b, v->s);
    break;
  case JV_ARR:
  case JV_OBJ:
    sb_add(b, v->t == JV_ARR ? "[" : "{", 1);
    for(i = 0; i < v->count; i++) {
      if(i)
        sb_add(b, ",", 1);
      indent(b, pretty, depth + 1);
      if(v->t == JV_OBJ) {
        write_string(b, v->keys[i]);
        sb_str(b, pretty ? ": " : ":");
      }
      write_value(b, v->items[i], pretty, depth + 1);
    }
    if(v->count)
      indent(b, pretty, depth);
    sb_add(b, v->t == JV_ARR ? "]" : "}", 1);
    break;
  }
}

char *jv_write(const jv *v, int pretty, size_t *len)
{
  sbuf b;
  memset(&b, 0, sizeof(b));
  write_value(&b, v, pretty, 0);
  if(pretty)
    sb_add(&b, "\n", 1);
  if(b.oom) {
    free(b.d);
    return NULL;
  }
  if(len)
    *len = b.n;
  return b.d;
}
