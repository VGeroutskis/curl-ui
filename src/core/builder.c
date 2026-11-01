#include "builder.h"
#include "options.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void request_init(request *r)
{
  memset(r, 0, sizeof(*r));
}

static void sel_free(sel_option *s)
{
  int i;
  for(i = 0; i < s->nvalues; i++)
    free(s->values[i]);
  free(s->values);
  s->values = NULL;
  s->nvalues = 0;
}

void request_clear_options(request *r)
{
  int i;
  for(i = 0; i < r->nsel; i++)
    sel_free(&r->sel[i]);
  r->nsel = 0;
}

void request_free(request *r)
{
  request_clear_options(r);
  free(r->sel);
  r->sel = NULL;
  r->capsel = 0;
}

int request_find(const request *r, int opt)
{
  int i;
  for(i = 0; i < r->nsel; i++)
    if(r->sel[i].opt == opt)
      return i;
  return -1;
}

char *request_add_value(sel_option *s)
{
  char **grown = realloc(s->values, sizeof(char *) * (size_t)(s->nvalues + 1));
  char *value;
  if(!grown)
    return NULL;
  s->values = grown;
  value = calloc(1, BUILDER_VALUE_CAP);
  if(!value)
    return NULL;
  s->values[s->nvalues++] = value;
  return value;
}

void request_remove_value(sel_option *s, int index)
{
  if(index < 0 || index >= s->nvalues)
    return;
  free(s->values[index]);
  memmove(&s->values[index], &s->values[index + 1],
          sizeof(char *) * (size_t)(s->nvalues - index - 1));
  s->nvalues--;
}

sel_option *request_add(request *r, int opt)
{
  sel_option *s;
  int slot = request_find(r, opt);
  if(slot >= 0)
    return &r->sel[slot];
  if(r->nsel == r->capsel) {
    int cap = r->capsel ? r->capsel * 2 : 16;
    sel_option *grown = realloc(r->sel, sizeof(sel_option) * (size_t)cap);
    if(!grown)
      return NULL;
    r->sel = grown;
    r->capsel = cap;
  }
  s = &r->sel[r->nsel++];
  memset(s, 0, sizeof(*s));
  s->opt = opt;
  if(g_options[opt].arg[0] && !request_add_value(s)) {
    r->nsel--;
    return NULL;
  }
  return s;
}

void request_remove(request *r, int opt)
{
  int slot = request_find(r, opt);
  if(slot < 0)
    return;
  sel_free(&r->sel[slot]);
  memmove(&r->sel[slot], &r->sel[slot + 1],
          sizeof(sel_option) * (size_t)(r->nsel - slot - 1));
  r->nsel--;
}

static char *dup_str(const char *s)
{
  size_t n = strlen(s) + 1;
  char *d = malloc(n);
  if(d)
    memcpy(d, s, n);
  return d;
}

static int push(arg_list *l, int *cap, const char *text, tok_kind kind, int opt)
{
  if(l->count == *cap) {
    int ncap = *cap ? *cap * 2 : 32;
    arg_token *grown = realloc(l->tok, sizeof(arg_token) * (size_t)ncap);
    if(!grown)
      return -1;
    l->tok = grown;
    *cap = ncap;
  }
  l->tok[l->count].text = dup_str(text);
  if(!l->tok[l->count].text)
    return -1;
  l->tok[l->count].kind = kind;
  l->tok[l->count].opt = opt;
  l->count++;
  return 0;
}

int builder_build(const request *r, const char *program, arg_list *out)
{
  int cap = 0, i, j, fail = 0;
  char flag[160];
  int req_opt = options_find("request");
  int head_opt = options_find("head");

  out->tok = NULL;
  out->count = 0;
  fail |= push(out, &cap, program, TOK_PROGRAM, -1);

  if(r->method[0] && request_find(r, req_opt) < 0) {
    if(!strcmp(r->method, "HEAD")) {
      if(request_find(r, head_opt) < 0)
        fail |= push(out, &cap, "--head", TOK_FLAG, head_opt);
    }
    else {
      fail |= push(out, &cap, "-X", TOK_FLAG, req_opt);
      fail |= push(out, &cap, r->method, TOK_VALUE, req_opt);
    }
  }

  for(i = 0; i < r->nsel; i++) {
    const sel_option *s = &r->sel[i];
    const opt_def *o = &g_options[s->opt];
    if(!o->arg[0]) {
      snprintf(flag, sizeof(flag), "--%s", (s->negated && o->neg[0]) ? o->neg : o->name);
      fail |= push(out, &cap, flag, TOK_FLAG, s->opt);
      continue;
    }
    snprintf(flag, sizeof(flag), "--%s", o->name);
    for(j = 0; j < s->nvalues; j++) {
      if(!s->values[j][0])
        continue;
      fail |= push(out, &cap, flag, TOK_FLAG, s->opt);
      fail |= push(out, &cap, s->values[j], TOK_VALUE, s->opt);
    }
  }

  if(r->url[0]) {
    /* a URL that looks like a flag must not be parsed as one */
    if(r->url[0] == '-') {
      int url_opt = options_find("url");
      fail |= push(out, &cap, "--url", TOK_FLAG, url_opt);
    }
    fail |= push(out, &cap, r->url, TOK_URL, -1);
  }
  if(fail) {
    arg_list_free(out);
    return -1;
  }
  return 0;
}

void arg_list_free(arg_list *l)
{
  int i;
  for(i = 0; i < l->count; i++)
    free(l->tok[i].text);
  free(l->tok);
  l->tok = NULL;
  l->count = 0;
}

const char **arg_list_argv(const arg_list *l, const char *const *extra)
{
  /* extra arguments go before the URL: curl reads them the same anywhere,
     and it keeps the URL last as in the preview */
  int nextra = 0, i, n = 0, last = l->count;
  const char **argv;
  while(extra && extra[nextra])
    nextra++;
  argv = malloc(sizeof(char *) * (size_t)(l->count + nextra + 1));
  if(!argv)
    return NULL;
  if(last > 0 && l->tok[last - 1].kind == TOK_URL) {
    last--;
    if(last > 0 && l->tok[last - 1].kind == TOK_FLAG &&
       !strcmp(l->tok[last - 1].text, "--url") && l->tok[last].text[0] == '-')
      last--;
  }
  for(i = 0; i < last; i++)
    argv[n++] = l->tok[i].text;
  for(i = 0; i < nextra; i++)
    argv[n++] = extra[i];
  for(i = last; i < l->count; i++)
    argv[n++] = l->tok[i].text;
  argv[n] = NULL;
  return argv;
}

char *shell_quote(const char *arg)
{
  static const char safe[] =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
    "-_./:=@%+,";
  size_t len = strlen(arg), extra = 0, i, n = 0;
  char *out;
  if(len && strspn(arg, safe) == len)
    return dup_str(arg);
  for(i = 0; i < len; i++)
    if(arg[i] == '\'')
      extra += 3;
  out = malloc(len + extra + 3);
  if(!out)
    return NULL;
  out[n++] = '\'';
  for(i = 0; i < len; i++) {
    if(arg[i] == '\'') {
      memcpy(out + n, "'\\''", 4);
      n += 4;
    }
    else
      out[n++] = arg[i];
  }
  out[n++] = '\'';
  out[n] = '\0';
  return out;
}

char *builder_preview(const arg_list *l)
{
  size_t cap = 1, n = 0;
  char *out;
  int i;
  char **quoted = calloc((size_t)l->count + 1, sizeof(char *));
  if(!quoted)
    return NULL;
  for(i = 0; i < l->count; i++) {
    quoted[i] = shell_quote(l->tok[i].text);
    if(quoted[i])
      cap += strlen(quoted[i]) + 1;
  }
  out = malloc(cap);
  if(out) {
    for(i = 0; i < l->count; i++) {
      size_t q;
      if(!quoted[i])
        continue;
      q = strlen(quoted[i]);
      if(n)
        out[n++] = ' ';
      memcpy(out + n, quoted[i], q);
      n += q;
    }
    out[n] = '\0';
  }
  for(i = 0; i < l->count; i++)
    free(quoted[i]);
  free(quoted);
  return out;
}
