#include "mask.h"
#include "options.h"
#include "urlquery.h"

#include <stdlib.h>
#include <string.h>

struct mask_set {
  char **s;
  size_t *n;
  int count;
};

char *base64_encode(const char *s, size_t len)
{
  static const char tab[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  char *out = malloc((len + 2) / 3 * 4 + 1), *d = out;
  size_t i;
  if(!out)
    return NULL;
  for(i = 0; i + 2 < len; i += 3) {
    unsigned v = ((unsigned)(unsigned char)s[i] << 16) | ((unsigned)(unsigned char)s[i + 1] << 8) |
                 (unsigned)(unsigned char)s[i + 2];
    *d++ = tab[v >> 18];
    *d++ = tab[(v >> 12) & 63];
    *d++ = tab[(v >> 6) & 63];
    *d++ = tab[v & 63];
  }
  if(i < len) {
    unsigned v = (unsigned)(unsigned char)s[i] << 16;
    if(i + 1 < len)
      v |= (unsigned)(unsigned char)s[i + 1] << 8;
    *d++ = tab[v >> 18];
    *d++ = tab[(v >> 12) & 63];
    *d++ = i + 1 < len ? tab[(v >> 6) & 63] : '=';
    *d++ = '=';
  }
  *d = '\0';
  return out;
}

/* Takes text. */
static void add_needle(mask_set *m, char *text)
{
  char **grown_s;
  size_t *grown_n;
  size_t n;
  int i;
  if(!text)
    return;
  n = strlen(text);
  for(i = 0; i < m->count; i++) {
    if(m->n[i] == n && !memcmp(m->s[i], text, n)) {
      free(text);
      return;
    }
  }
  grown_s = realloc(m->s, sizeof(char *) * (size_t)(m->count + 1));
  if(grown_s)
    m->s = grown_s;
  grown_n = realloc(m->n, sizeof(size_t) * (size_t)(m->count + 1));
  if(grown_n)
    m->n = grown_n;
  if(!grown_s || !grown_n) {
    free(text);
    return;
  }
  /* longest first, so that a secret inside another cannot leave part of it showing */
  for(i = m->count; i > 0 && m->n[i - 1] < n; i--) {
    m->s[i] = m->s[i - 1];
    m->n[i] = m->n[i - 1];
  }
  m->s[i] = text;
  m->n[i] = n;
  m->count++;
}

static char *copy_str(const char *s)
{
  size_t n = strlen(s) + 1;
  char *d = malloc(n);
  if(d)
    memcpy(d, s, n);
  return d;
}

mask_set *mask_build(const var_env *e, const arg_list *real)
{
  mask_set *m = calloc(1, sizeof(*m));
  int user = options_find("user"), proxy_user = options_find("proxy-user");
  int i, k;
  if(!m)
    return NULL;
  for(i = 0; i < e->n; i++) {
    const var_t *v = &e->v[i];
    if(!v->secret || strlen(v->value) < MASK_MIN_LEN)
      continue;
    add_needle(m, copy_str(v->value));
    add_needle(m, pct_encode(v->value));
    /* a secret inside a user:password goes out as base64 of the whole */
    for(k = 1; real && k < real->count; k++) {
      const arg_token *t = &real->tok[k];
      if(t->kind == TOK_VALUE && (t->opt == user || t->opt == proxy_user) && strstr(t->text, v->value))
        add_needle(m, base64_encode(t->text, strlen(t->text)));
    }
  }
  return m;
}

void mask_free(mask_set *m)
{
  int i;
  if(!m)
    return;
  for(i = 0; i < m->count; i++) {
    memset(m->s[i], 0, m->n[i]);
    free(m->s[i]);
  }
  free(m->s);
  free(m->n);
  free(m);
}

void mask_apply(const mask_set *m, char *buf, size_t len)
{
  char *orig;
  int i;
  if(!m || !buf || !m->count || !len)
    return;
  /* searched in the original, so that secrets that overlap are each still found */
  orig = malloc(len);
  if(!orig) {
    memset(buf, '*', len);
    return;
  }
  memcpy(orig, buf, len);
  for(i = 0; i < m->count; i++) {
    size_t n = m->n[i], at = 0;
    const char first = m->s[i][0];
    while(n && at + n <= len) {
      char *hit = memchr(orig + at, first, len - at - n + 1);
      if(!hit)
        break;
      at = (size_t)(hit - orig);
      if(!memcmp(hit, m->s[i], n))
        memset(buf + at, '*', n);
      at++;
    }
  }
  free(orig);
}
