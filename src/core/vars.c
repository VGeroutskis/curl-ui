#include "vars.h"
#include "fileio.h"
#include "json.h"
#include "options.h"
#include "urlquery.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VARS_FILE_MAX ((size_t)1 << 30)
#define SECRET_MASK "********"

static char *dup_cap(const char *s, size_t *cap)
{
  size_t n = strlen(s) + 1;
  size_t want = n < VAR_NAME_CAP ? VAR_NAME_CAP : n;
  char *d = malloc(want);
  if(d) {
    memcpy(d, s, n);
    if(cap)
      *cap = want;
  }
  return d;
}

static void env_free(var_env *e)
{
  int i;
  for(i = 0; i < e->n; i++) {
    free(e->v[i].name);
    free(e->v[i].value);
  }
  free(e->v);
  free(e->name);
  memset(e, 0, sizeof(*e));
}

static int env_push(var_store *s, const char *name)
{
  var_env *grown = realloc(s->e, sizeof(var_env) * (size_t)(s->n + 1));
  var_env *e;
  if(!grown)
    return -1;
  s->e = grown;
  e = &s->e[s->n];
  memset(e, 0, sizeof(*e));
  e->name = dup_cap(name, &e->name_cap);
  if(!e->name)
    return -1;
  return s->n++;
}

void vars_init(var_store *s)
{
  memset(s, 0, sizeof(*s));
  env_push(s, "Default");
}

void vars_free(var_store *s)
{
  int i;
  for(i = 0; i < s->n; i++)
    env_free(&s->e[i]);
  free(s->e);
  memset(s, 0, sizeof(*s));
}

int vars_name_ok(const char *name)
{
  if(!*name)
    return 0;
  for(; *name; name++) {
    unsigned char c = (unsigned char)*name;
    if(!isalnum(c) || c >= 0x80) {
      if(c != '_' && c != '.' && c != '-')
        return 0;
    }
  }
  return 1;
}

var_t *var_find(const var_env *e, const char *name)
{
  int i;
  for(i = 0; i < e->n; i++)
    if(!strcmp(e->v[i].name, name))
      return &e->v[i];
  return NULL;
}

static var_t *var_find_n(const var_env *e, const char *name, size_t n)
{
  int i;
  for(i = 0; i < e->n; i++)
    if(strlen(e->v[i].name) == n && !memcmp(e->v[i].name, name, n))
      return &e->v[i];
  return NULL;
}

var_t *var_set(var_env *e, const char *name, const char *value, int secret)
{
  var_t *v = var_find(e, name);
  char *copy;
  size_t cap;
  if(v) {
    copy = dup_cap(value, &cap);
    if(!copy)
      return NULL;
    free(v->value);
    v->value = copy;
    v->value_cap = cap;
    v->secret = secret;
    return v;
  }
  if(e->n == e->cap) {
    int ncap = e->cap ? e->cap * 2 : 16;
    var_t *grown = realloc(e->v, sizeof(var_t) * (size_t)ncap);
    if(!grown)
      return NULL;
    e->v = grown;
    e->cap = ncap;
  }
  v = &e->v[e->n];
  memset(v, 0, sizeof(*v));
  v->name = dup_cap(name, &v->name_cap);
  v->value = dup_cap(value, &v->value_cap);
  if(!v->name || !v->value) {
    free(v->name);
    free(v->value);
    return NULL;
  }
  v->secret = secret;
  e->n++;
  return v;
}

void var_remove(var_env *e, int i)
{
  if(i < 0 || i >= e->n)
    return;
  free(e->v[i].name);
  free(e->v[i].value);
  memmove(&e->v[i], &e->v[i + 1], sizeof(var_t) * (size_t)(e->n - i - 1));
  e->n--;
}

static int env_name_free(const var_store *s, const char *name, int except)
{
  int i;
  if(!*name)
    return 0;
  for(i = 0; i < s->n; i++)
    if(i != except && !strcmp(s->e[i].name, name))
      return 0;
  return 1;
}

int env_add(var_store *s, const char *name)
{
  if(!env_name_free(s, name, -1))
    return -1;
  return env_push(s, name);
}

int env_copy(var_store *s, int i, const char *name)
{
  int to, k;
  if(i < 0 || i >= s->n || !env_name_free(s, name, -1))
    return -1;
  to = env_push(s, name);
  if(to < 0)
    return -1;
  for(k = 0; k < s->e[i].n; k++)
    var_set(&s->e[to], s->e[i].v[k].name, s->e[i].v[k].value, s->e[i].v[k].secret);
  return to;
}

int env_rename(var_store *s, int i, const char *name)
{
  char *copy;
  size_t cap;
  if(i < 0 || i >= s->n || !env_name_free(s, name, i))
    return -1;
  copy = dup_cap(name, &cap);
  if(!copy)
    return -1;
  free(s->e[i].name);
  s->e[i].name = copy;
  s->e[i].name_cap = cap;
  return 0;
}

int env_remove(var_store *s, int i)
{
  if(i < 0 || i >= s->n || s->n == 1)
    return -1;
  env_free(&s->e[i]);
  memmove(&s->e[i], &s->e[i + 1], sizeof(var_env) * (size_t)(s->n - i - 1));
  s->n--;
  if(s->active > i)
    s->active--;
  if(s->active >= s->n)
    s->active = s->n - 1;
  return 0;
}

int vars_suggest_secret(const char *name)
{
  static const char *const words[] = { "token", "secret", "password", "passwd", "key", "auth", NULL };
  char low[VAR_NAME_CAP];
  size_t i;
  int w;
  for(i = 0; name[i] && i + 1 < sizeof(low); i++)
    low[i] = (char)tolower((unsigned char)name[i]);
  low[i] = '\0';
  for(w = 0; words[w]; w++)
    if(strstr(low, words[w]))
      return 1;
  return 0;
}

/* Finds the next {{name}} from s. Returns its start or NULL, with the name's start and length. */
static const char *next_var(const char *s, const char **name, size_t *n)
{
  const char *p = s;
  while((p = strstr(p, "{{")) != NULL) {
    const char *b = p + 2, *e = b;
    while(*e && *e != '}' && *e != '{' && (isalnum((unsigned char)*e) || *e == '_' || *e == '.' || *e == '-'))
      e++;
    if(e > b && e[0] == '}' && e[1] == '}') {
      *name = b;
      *n = (size_t)(e - b);
      return p;
    }
    p++;
  }
  return NULL;
}

static int list_has(const str_list *l, const char *name, size_t n)
{
  int i;
  for(i = 0; i < l->n; i++)
    if(strlen(l->s[i]) == n && !memcmp(l->s[i], name, n))
      return 1;
  return 0;
}

int vars_missing(const var_env *e, const arg_list *tmpl, str_list *out)
{
  int i;
  out->s = NULL;
  out->n = 0;
  for(i = 1; i < tmpl->count; i++) {
    const char *s = tmpl->tok[i].text, *name, *p;
    size_t n;
    if(tmpl->tok[i].kind == TOK_FLAG)
      continue;
    while((p = next_var(s, &name, &n)) != NULL) {
      s = name + n + 2;
      /* a secret with no value is one still to be given: sent empty it is only ever a failure */
      const var_t *known = var_find_n(e, name, n);
      if((!known || (known->secret && !known->value[0])) && !list_has(out, name, n)) {
        char **grown = realloc(out->s, sizeof(char *) * (size_t)(out->n + 1));
        char *copy = malloc(n + 1);
        if(!grown || !copy) {
          free(copy);
          if(grown)
            out->s = grown;
          str_list_free(out);
          return -1;
        }
        out->s = grown;
        memcpy(copy, name, n);
        copy[n] = '\0';
        out->s[out->n++] = copy;
      }
      (void)p;
    }
  }
  return 0;
}

void str_list_free(str_list *l)
{
  int i;
  for(i = 0; i < l->n; i++)
    free(l->s[i]);
  free(l->s);
  l->s = NULL;
  l->n = 0;
}

typedef struct {
  char *d;
  size_t n, cap;
} grow_buf;

static int gb_add(grow_buf *b, const char *s, size_t n)
{
  if(b->n + n + 1 > b->cap) {
    size_t cap = b->cap ? b->cap * 2 : 256;
    char *d;
    while(cap < b->n + n + 1)
      cap *= 2;
    d = realloc(b->d, cap);
    if(!d)
      return -1;
    b->d = d;
    b->cap = cap;
  }
  memcpy(b->d + b->n, s, n);
  b->n += n;
  b->d[b->n] = '\0';
  return 0;
}

static void track_url(const char *s, size_t n, int *in_query)
{
  size_t i;
  for(i = 0; i < n; i++) {
    if(s[i] == '?')
      *in_query = 1;
    else if(s[i] == '#')
      *in_query = 0;
  }
}

/* One token, for real and for shown. */
static int subst(const var_env *e, const char *text, int is_url, char **real, char **shown)
{
  grow_buf r, s;
  const char *p = text, *name, *at;
  size_t n;
  int in_query = 0, fail = 0;
  memset(&r, 0, sizeof(r));
  memset(&s, 0, sizeof(s));
  fail |= gb_add(&r, "", 0);
  fail |= gb_add(&s, "", 0);
  while(!fail && (at = next_var(p, &name, &n)) != NULL) {
    const var_t *v = var_find_n(e, name, n);
    size_t lit = (size_t)(at - p);
    fail |= gb_add(&r, p, lit);
    fail |= gb_add(&s, p, lit);
    if(is_url)
      track_url(p, lit, &in_query);
    if(!v) {
      fail |= gb_add(&r, at, n + 4);
      fail |= gb_add(&s, at, n + 4);
    }
    else if(is_url && in_query) {
      char *enc = pct_encode(v->value);
      if(!enc)
        fail = 1;
      else {
        fail |= gb_add(&r, enc, strlen(enc));
        if(v->secret)
          fail |= gb_add(&s, SECRET_MASK, strlen(SECRET_MASK));
        else
          fail |= gb_add(&s, enc, strlen(enc));
        free(enc);
      }
    }
    else {
      size_t vn = strlen(v->value);
      fail |= gb_add(&r, v->value, vn);
      if(v->secret)
        fail |= gb_add(&s, SECRET_MASK, strlen(SECRET_MASK));
      else
        fail |= gb_add(&s, v->value, vn);
      if(is_url)
        track_url(v->value, vn, &in_query);
    }
    p = name + n + 2;
  }
  if(!fail) {
    fail |= gb_add(&r, p, strlen(p));
    fail |= gb_add(&s, p, strlen(p));
  }
  if(fail) {
    free(r.d);
    free(s.d);
    return -1;
  }
  *real = r.d;
  *shown = s.d;
  return 0;
}

int vars_apply(const var_env *e, const arg_list *tmpl, arg_list *real, arg_list *shown)
{
  int url_opt = options_find("url");
  int i;
  real->count = shown->count = 0;
  real->tok = calloc((size_t)tmpl->count + 1, sizeof(arg_token));
  shown->tok = calloc((size_t)tmpl->count + 1, sizeof(arg_token));
  if(!real->tok || !shown->tok)
    goto oom;
  for(i = 0; i < tmpl->count; i++) {
    const arg_token *t = &tmpl->tok[i];
    int is_url = t->kind == TOK_URL || (t->kind == TOK_VALUE && t->opt == url_opt);
    if(t->kind == TOK_FLAG || t->kind == TOK_PROGRAM) {
      size_t n = strlen(t->text) + 1;
      real->tok[i].text = malloc(n);
      shown->tok[i].text = malloc(n);
      if(!real->tok[i].text || !shown->tok[i].text) {
        real->count = shown->count = i + 1;
        goto oom;
      }
      memcpy(real->tok[i].text, t->text, n);
      memcpy(shown->tok[i].text, t->text, n);
    }
    else if(subst(e, t->text, is_url, &real->tok[i].text, &shown->tok[i].text))
      goto oom;
    real->tok[i].kind = shown->tok[i].kind = t->kind;
    real->tok[i].opt = shown->tok[i].opt = t->opt;
    real->count = shown->count = i + 1;
  }
  return 0;
oom:
  arg_list_free(real);
  arg_list_free(shown);
  return -1;
}

int vars_copy_used(const var_env *from, var_env *to, const arg_list *tmpl)
{
  int i, copied = 0;
  for(i = 1; i < tmpl->count; i++) {
    const char *t = tmpl->tok[i].text, *name;
    size_t n;
    if(tmpl->tok[i].kind == TOK_FLAG)
      continue;
    while(next_var(t, &name, &n)) {
      const var_t *v = var_find_n(from, name, n);
      if(v && !var_find_n(to, name, n) && var_set(to, v->name, v->value, v->secret))
        copied++;
      t = name + n + 2;
    }
  }
  return copied;
}

/* ---- the book ---- */

void book_init(var_book *b)
{
  memset(b, 0, sizeof(*b));
}

void book_free(var_book *b)
{
  int i;
  for(i = 0; i < b->n; i++) {
    vars_free(&b->e[i]->st);
    free(b->e[i]);
  }
  free(b->e);
  memset(b, 0, sizeof(*b));
}

var_store *book_get(var_book *b, const char *id)
{
  var_book_entry *e, **g;
  int i;
  for(i = 0; i < b->n; i++)
    if(!strcmp(b->e[i]->id, id))
      return &b->e[i]->st;
  e = calloc(1, sizeof(*e));
  g = realloc(b->e, sizeof(*g) * (size_t)(b->n + 1));
  if(g)
    b->e = g;
  if(!e || !g) {
    free(e);
    return NULL;
  }
  snprintf(e->id, sizeof(e->id), "%s", id);
  vars_init(&e->st);
  b->e[b->n++] = e;
  return &e->st;
}

void book_remove(var_book *b, const char *id)
{
  int i;
  for(i = 0; i < b->n; i++) {
    if(!strcmp(b->e[i]->id, id)) {
      vars_free(&b->e[i]->st);
      free(b->e[i]);
      memmove(&b->e[i], &b->e[i + 1], sizeof(b->e[0]) * (size_t)(b->n - i - 1));
      b->n--;
      return;
    }
  }
}

/* ---- file ---- */

static jv *store_to_jv(const var_store *s)
{
  jv *root = jv_new(JV_OBJ), *envs = jv_new(JV_ARR);
  int i, k;
  jv_set(root, "active", jv_new_str(s->e[s->active].name));
  for(i = 0; i < s->n; i++) {
    jv *env = jv_new(JV_OBJ), *vars = jv_new(JV_ARR);
    jv_set(env, "name", jv_new_str(s->e[i].name));
    for(k = 0; k < s->e[i].n; k++) {
      const var_t *v = &s->e[i].v[k];
      jv *row;
      if(!vars_name_ok(v->name) || var_find(&s->e[i], v->name) != v)
        continue;
      row = jv_new(JV_OBJ);
      jv_set(row, "name", jv_new_str(v->name));
      jv_set(row, "value", jv_new_str(v->value));
      jv_set(row, "secret", jv_new_bool(v->secret));
      jv_push(vars, row);
    }
    jv_set(env, "variables", vars);
    jv_push(envs, env);
  }
  jv_set(root, "environments", envs);
  return root;
}

int book_save(const var_book *b, const char *path)
{
  jv *root = jv_new(JV_OBJ), *colls = jv_new(JV_OBJ);
  char *text;
  size_t n = 0;
  int i, rc;
  jv_set(root, "format", jv_new_str("curl-ui-variables"));
  jv_set(root, "version", jv_new_num(2));
  for(i = 0; i < b->n; i++)
    jv_set(colls, b->e[i]->id, store_to_jv(&b->e[i]->st));
  jv_set(root, "collections", colls);
  text = jv_write(root, 1, &n);
  jv_free(root);
  if(!text)
    return -1;
  rc = file_write_private(path, text, n);
  memset(text, 0, n);
  free(text);
  return rc;
}

static void store_from_jv(var_store *s, const jv *root)
{
  const jv *envs = jv_get(root, "environments");
  const char *active;
  int i, k;
  vars_free(s);
  for(i = 0; i < jv_len(envs); i++) {
    const jv *env = jv_at(envs, i), *vars = jv_get(env, "variables");
    const char *name = jv_str(jv_get(env, "name"));
    int to;
    if(!name || !env_name_free(s, name, -1))
      continue;
    to = env_push(s, name);
    if(to < 0)
      break;
    for(k = 0; k < jv_len(vars); k++) {
      const jv *row = jv_at(vars, k);
      const char *vn = jv_str(jv_get(row, "name")), *vv = jv_str(jv_get(row, "value"));
      if(vn && vv && vars_name_ok(vn))
        var_set(&s->e[to], vn, vv, jv_bool(jv_get(row, "secret")));
    }
  }
  if(!s->n)
    env_push(s, "Default");
  active = jv_str(jv_get(root, "active"));
  for(i = 0; active && i < s->n; i++)
    if(!strcmp(s->e[i].name, active))
      s->active = i;
}

int book_load(var_book *b, const char *path, char *err, size_t n)
{
  size_t len = 0;
  char *text = file_read_all(path, &len, VARS_FILE_MAX);
  char perr[128], moved[2048];
  jv *root, *colls;
  int i;
  if(!text)
    return 0;
  perr[0] = '\0';
  root = jv_parse(text, len, perr, sizeof(perr));
  free(text);
  colls = jv_get(root, "collections");
  if(jv_kind(colls) != JV_OBJ && jv_kind(jv_get(root, "environments")) != JV_ARR) {
    moved[0] = '\0';
    if(file_quarantine(path, moved, sizeof(moved)))
      moved[0] = '\0';
    snprintf(err, n, "%s could not be read (%s) and was moved to %s", path, perr[0] ? perr : "not a variables file",
             moved[0] ? moved : "nowhere");
    jv_free(root);
    return -1;
  }
  if(jv_kind(colls) == JV_OBJ) {
    for(i = 0; i < jv_len(colls); i++) {
      const char *id = jv_key_at(colls, i);
      var_store *s = strlen(id) == 16 ? book_get(b, id) : NULL;
      if(s)
        store_from_jv(s, jv_at(colls, i));
    }
  }
  else {
    var_store *s = book_get(b, VARS_DEFAULT_ID);
    if(s)
      store_from_jv(s, root);
  }
  jv_free(root);
  return 0;
}
