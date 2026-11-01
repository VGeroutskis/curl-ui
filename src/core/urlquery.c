#include "urlquery.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int unreserved(unsigned char c)
{
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
         c == '-' || c == '_' || c == '.' || c == '~';
}

char *pct_encode(const char *s)
{
  static const char hex[] = "0123456789ABCDEF";
  char *out = malloc(strlen(s) * 3 + 1), *d = out;
  if(!out)
    return NULL;
  for(; *s; s++) {
    unsigned char c = (unsigned char)*s;
    if(unreserved(c))
      *d++ = (char)c;
    else {
      *d++ = '%';
      *d++ = hex[c >> 4];
      *d++ = hex[c & 15];
    }
  }
  *d = '\0';
  return out;
}

static int hexval(char c)
{
  if(c >= '0' && c <= '9')
    return c - '0';
  if(c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if(c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

char *pct_decode(const char *s, int plus_is_space)
{
  char *out = malloc(strlen(s) + 1), *d = out;
  if(!out)
    return NULL;
  while(*s) {
    if(*s == '%' && hexval(s[1]) >= 0 && hexval(s[2]) >= 0) {
      /* %00 cannot live in a C string: left as written */
      if(s[1] == '0' && s[2] == '0') {
        *d++ = *s++;
        continue;
      }
      *d++ = (char)(hexval(s[1]) * 16 + hexval(s[2]));
      s += 3;
    }
    else if(*s == '+' && plus_is_space) {
      *d++ = ' ';
      s++;
    }
    else
      *d++ = *s++;
  }
  *d = '\0';
  return out;
}

static char *dup_field(const char *s, size_t *cap)
{
  size_t n = strlen(s) + 1, want = n < 64 ? 64 : n;
  char *d = malloc(want);
  if(d) {
    memcpy(d, s, n);
    *cap = want;
  }
  return d;
}

kv_row *kv_add(kv_list *l, const char *key, const char *value, int enabled)
{
  kv_row *row;
  if(l->n == l->cap) {
    int cap = l->cap ? l->cap * 2 : 8;
    kv_row *g = realloc(l->r, sizeof(kv_row) * (size_t)cap);
    if(!g)
      return NULL;
    l->r = g;
    l->cap = cap;
  }
  row = &l->r[l->n];
  memset(row, 0, sizeof(*row));
  row->key = dup_field(key, &row->key_cap);
  row->value = dup_field(value, &row->value_cap);
  if(!row->key || !row->value) {
    free(row->key);
    free(row->value);
    return NULL;
  }
  row->enabled = enabled;
  l->n++;
  return row;
}

void kv_remove(kv_list *l, int i)
{
  if(i < 0 || i >= l->n)
    return;
  free(l->r[i].key);
  free(l->r[i].value);
  memmove(&l->r[i], &l->r[i + 1], sizeof(kv_row) * (size_t)(l->n - i - 1));
  l->n--;
}

void kv_free(kv_list *l)
{
  int i;
  for(i = 0; i < l->n; i++) {
    free(l->r[i].key);
    free(l->r[i].value);
  }
  free(l->r);
  memset(l, 0, sizeof(*l));
}

/* Decoding around {{name}}: what is inside stays as written. */
static char *decode_part(const char *s, size_t n)
{
  char *raw = malloc(n + 1), *out;
  if(!raw)
    return NULL;
  memcpy(raw, s, n);
  raw[n] = '\0';
  out = pct_decode(raw, 1);
  free(raw);
  return out;
}

int query_parse(const char *url, kv_list *rows)
{
  const char *q = strchr(url, '?');
  const char *end;
  memset(rows, 0, sizeof(*rows));
  if(!q)
    return 0;
  q++;
  end = strchr(q, '#');
  if(!end)
    end = q + strlen(q);
  while(q < end) {
    const char *amp = memchr(q, '&', (size_t)(end - q));
    const char *stop = amp ? amp : end;
    const char *eq = memchr(q, '=', (size_t)(stop - q));
    char *k = decode_part(q, (size_t)((eq ? eq : stop) - q));
    char *v = eq ? decode_part(eq + 1, (size_t)(stop - eq - 1)) : decode_part("", 0);
    if(!k || !v || !kv_add(rows, k, v, 1)) {
      free(k);
      free(v);
      kv_free(rows);
      return -1;
    }
    free(k);
    free(v);
    q = amp ? amp + 1 : end;
  }
  return 0;
}

/* Is there a {{name}} here, of a name a variable can have? Its length, or 0. */
static size_t var_at(const char *s)
{
  const char *e = s + 2;
  if(s[0] != '{' || s[1] != '{')
    return 0;
  while((*e >= 'a' && *e <= 'z') || (*e >= 'A' && *e <= 'Z') || (*e >= '0' && *e <= '9') || *e == '_' || *e == '.' ||
        *e == '-')
    e++;
  return e > s + 2 && e[0] == '}' && e[1] == '}' ? (size_t)(e - s) + 2 : 0;
}

/* Bytes needed for s encoded, around any variables, written to d when given. */
static size_t encode_keeping_vars(const char *s, char *d)
{
  static const char hex[] = "0123456789ABCDEF";
  size_t n = 0;
  while(*s) {
    size_t v = var_at(s);
    if(v) {
      if(d)
        memcpy(d + n, s, v);
      n += v;
      s += v;
    }
    else {
      unsigned char c = (unsigned char)*s++;
      if(unreserved(c)) {
        if(d)
          d[n] = (char)c;
        n++;
      }
      else {
        if(d) {
          d[n] = '%';
          d[n + 1] = hex[c >> 4];
          d[n + 2] = hex[c & 15];
        }
        n += 3;
      }
    }
  }
  return n;
}

char *query_build(const char *url, const kv_list *rows)
{
  const char *q = strchr(url, '?');
  const char *hash = strchr(url, '#');
  size_t base = q && (!hash || q < hash) ? (size_t)(q - url) : (hash ? (size_t)(hash - url) : strlen(url));
  size_t total = base + (hash ? strlen(hash) : 0) + 2;
  char *out, *d;
  int i, first = 1;
  for(i = 0; i < rows->n; i++)
    if(rows->r[i].enabled)
      total += encode_keeping_vars(rows->r[i].key, NULL) + encode_keeping_vars(rows->r[i].value, NULL) + 2;
  out = malloc(total);
  if(!out)
    return NULL;
  memcpy(out, url, base);
  d = out + base;
  for(i = 0; i < rows->n; i++) {
    const kv_row *r = &rows->r[i];
    if(!r->enabled || (!r->key[0] && !r->value[0]))
      continue;
    *d++ = first ? '?' : '&';
    first = 0;
    d += encode_keeping_vars(r->key, d);
    *d++ = '=';
    d += encode_keeping_vars(r->value, d);
  }
  if(hash) {
    size_t hn = strlen(hash);
    memcpy(d, hash, hn);
    d += hn;
  }
  *d = '\0';
  return out;
}
