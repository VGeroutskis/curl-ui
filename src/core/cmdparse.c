#include "cmdparse.h"
#include "options.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  char **s;
  int n;
} words;

static void words_free(words *w)
{
  int i;
  for(i = 0; i < w->n; i++)
    free(w->s[i]);
  free(w->s);
  memset(w, 0, sizeof(*w));
}

static int words_push(words *w, const char *buf, size_t n)
{
  char *d = malloc(n + 1);
  char **g;
  if(!d)
    return -1;
  memcpy(d, buf, n);
  d[n] = '\0';
  g = realloc(w->s, sizeof(char *) * (size_t)(w->n + 1));
  if(!g) {
    free(d);
    return -1;
  }
  w->s = g;
  w->s[w->n++] = d;
  return 0;
}

/* Splits like a POSIX shell would, and refuses what a shell would act on. */
static int split_words(const char *t, words *w, char *err, size_t errn)
{
  size_t cap = strlen(t) + 1, n = 0;
  char *buf = malloc(cap);
  int in_word = 0;
  if(!buf)
    return -1;
  while(*t) {
    char c = *t;
    if(c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      if(in_word && words_push(w, buf, n))
        goto oom;
      in_word = 0;
      n = 0;
      t++;
    }
    else if(c == '\'') {
      const char *end = strchr(t + 1, '\'');
      size_t len;
      if(!end) {
        snprintf(err, errn, "A quote is left open");
        goto fail;
      }
      len = (size_t)(end - t - 1);
      memcpy(buf + n, t + 1, len);
      n += len;
      in_word = 1;
      t = end + 1;
    }
    else if(c == '"') {
      t++;
      in_word = 1;
      while(*t && *t != '"') {
        if(*t == '$' || *t == '`') {
          snprintf(err, errn, "%c inside double quotes needs a shell to mean anything: use the value itself", *t);
          goto fail;
        }
        if(*t == '\\' && (t[1] == '"' || t[1] == '\\' || t[1] == '$' || t[1] == '`')) {
          buf[n++] = t[1];
          t += 2;
        }
        else if(*t == '\\' && t[1] == '\n')
          t += 2;
        else if(*t == '\\' && t[1] == '\r' && t[2] == '\n')
          t += 3;
        else
          buf[n++] = *t++;
      }
      if(!*t) {
        snprintf(err, errn, "A quote is left open");
        goto fail;
      }
      t++;
    }
    else if(c == '\\') {
      if(t[1] == '\n')
        t += 2;
      else if(t[1] == '\r' && t[2] == '\n')
        t += 3;
      else if(t[1]) {
        buf[n++] = t[1];
        in_word = 1;
        t += 2;
      }
      else
        t++;
    }
    else if(c == '$' || c == '`' || c == '|' || c == ';' || c == '&' || c == '<' || c == '>') {
      snprintf(err, errn, "%c needs a shell to mean anything, and none is used here: paste only the curl command", c);
      goto fail;
    }
    else if(c == '^' && (t[1] == '\n' || t[1] == '\r')) {
      snprintf(err, errn, "^ at the end of a line is Windows cmd syntax, which is not supported: copy the command for bash");
      goto fail;
    }
    else {
      buf[n++] = c;
      in_word = 1;
      t++;
    }
  }
  if(in_word && words_push(w, buf, n))
    goto oom;
  free(buf);
  return 0;
oom:
  snprintf(err, errn, "Out of memory");
fail:
  free(buf);
  return -1;
}

static int is_menu_method(const char *m)
{
  static const char *const known[] = { "GET", "POST", "PUT", "PATCH", "DELETE", "HEAD", "OPTIONS", NULL };
  int i;
  for(i = 0; known[i]; i++)
    if(!strcmp(known[i], m))
      return 1;
  return 0;
}

static int short_option(char c)
{
  int i;
  for(i = 0; i < g_option_count; i++)
    if(g_options[i].short_flag[0] == c && !g_options[i].short_flag[1])
      return i;
  return -1;
}

static int set_url(request *r, const char *url, char *err, size_t errn)
{
  if(r->url[0]) {
    snprintf(err, errn, "More than one URL: open them one at a time");
    return -1;
  }
  return request_set_url(r, url);
}

static int apply(request *r, int opt, const char *value, int negated, char *method, size_t methodn, char *err, size_t errn)
{
  const opt_def *o = &g_options[opt];
  sel_option *s;
  if(!strcmp(o->name, "url"))
    return set_url(r, value, err, errn);
  if(!strcmp(o->name, "request") && is_menu_method(value)) {
    snprintf(method, methodn, "%s", value);
    return 0;
  }
  {
    int fresh = request_find(r, opt) < 0;
    s = request_add(r, opt);
    if(!s) {
      snprintf(err, errn, "Out of memory");
      return -1;
    }
    s->negated = negated;
    if(value) {
      if(fresh || !o->repeatable)
        request_set_value(s, 0, value);
      else
        request_append_value(s, value);
    }
  }
  return 0;
}

static int parse_args(const words *w, int start, request *r, char *method, size_t methodn, char *err, size_t errn)
{
  int i, only_urls = 0;
  for(i = start; i < w->n; i++) {
    const char *a = w->s[i];
    if(only_urls || a[0] != '-' || !a[1]) {
      if(set_url(r, a, err, errn))
        return -1;
    }
    else if(!strcmp(a, "--"))
      only_urls = 1;
    else if(a[1] == '-') {
      int opt = options_find(a + 2), negated = 0;
      if(opt < 0 && !strncmp(a + 2, "no-", 3)) {
        opt = options_find(a + 5);
        negated = 1;
        if(opt >= 0 && strcmp(g_options[opt].neg, a + 2))
          opt = -1;
      }
      if(opt < 0) {
        snprintf(err, errn, "Unknown option: %s", a);
        return -1;
      }
      if(!strcmp(g_options[opt].name, "next")) {
        snprintf(err, errn, "--next sends several requests: open them one at a time");
        return -1;
      }
      if(g_options[opt].arg[0] && !negated) {
        if(i + 1 >= w->n) {
          snprintf(err, errn, "%s needs a value", a);
          return -1;
        }
        if(apply(r, opt, w->s[++i], 0, method, methodn, err, errn))
          return -1;
      }
      else if(apply(r, opt, NULL, negated, method, methodn, err, errn))
        return -1;
    }
    else {
      const char *p;
      for(p = a + 1; *p; p++) {
        int opt = short_option(*p);
        if(opt < 0) {
          snprintf(err, errn, "Unknown option: -%c", *p);
          return -1;
        }
        if(g_options[opt].arg[0]) {
          const char *value = p[1] ? p + 1 : NULL;
          if(!value) {
            if(i + 1 >= w->n) {
              snprintf(err, errn, "-%c needs a value", *p);
              return -1;
            }
            value = w->s[++i];
          }
          if(apply(r, opt, value, 0, method, methodn, err, errn))
            return -1;
          break;
        }
        if(apply(r, opt, NULL, 0, method, methodn, err, errn))
          return -1;
      }
    }
  }
  return 0;
}

int cmd_parse(const char *text, request *r, char *method, size_t methodn, char *err, size_t errn)
{
  words w;
  int start = 0, rc = -1;
  memset(&w, 0, sizeof(w));
  method[0] = '\0';
  err[0] = '\0';
  if(split_words(text, &w, err, errn)) {
    words_free(&w);
    return -1;
  }
  if(w.n) {
    const char *first = w.s[0];
    const char *base = strrchr(first, '/');
    base = base ? base + 1 : first;
    if(!strcmp(base, "curl") || !strcmp(base, "curl.exe"))
      start = 1;
  }
  if(start >= w.n)
    snprintf(err, errn, "The command is empty");
  else
    rc = parse_args(&w, start, r, method, methodn, err, errn);
  words_free(&w);
  if(rc) {
    request_clear_options(r);
    r->url[0] = '\0';
    method[0] = '\0';
  }
  return rc;
}
