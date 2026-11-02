#include "oas_import.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "builder.h"
#include "forms.h"
#include "oas_model.h"
#include "options.h"
#include "reqjson.h"
#include "urlquery.h"
#include "yaml.h"

/* What a name in the file becomes as a variable: anything else in it becomes _. */
static void var_name(const char *name, char *out, size_t n)
{
  size_t i;
  for(i = 0; name[i] && i + 1 < n; i++) {
    unsigned char c = (unsigned char)name[i];
    out[i] = (char)(isalnum(c) || c == '_' || c == '.' || c == '-' ? c : '_');
  }
  out[i] = '\0';
  if(!i)
    snprintf(out, n, "value");
}

static char *braced(const char *name)
{
  char v[VAR_NAME_CAP];
  size_t n;
  char *out;
  var_name(name, v, sizeof(v));
  n = strlen(v) + 5;
  out = malloc(n);
  if(out)
    snprintf(out, n, "{{%s}}", v);
  return out;
}

/* {x} in a path or a server's URL becomes {{x}}. */
static char *template_vars(const char *s)
{
  size_t n = strlen(s);
  char *out = malloc(n * 2 + 16);
  size_t w = 0;
  const char *c = s;
  if(!out)
    return NULL;
  while(*c) {
    const char *end = *c == '{' ? strchr(c, '}') : NULL;
    if(end) {
      char name[VAR_NAME_CAP], v[VAR_NAME_CAP];
      size_t len = (size_t)(end - c - 1);
      if(len >= sizeof(name))
        len = sizeof(name) - 1;
      memcpy(name, c + 1, len);
      name[len] = '\0';
      var_name(name, v, sizeof(v));
      w += (size_t)sprintf(out + w, "{{%s}}", v);
      c = end + 1;
    }
    else
      out[w++] = *c++;
  }
  out[w] = '\0';
  return out;
}

static void add_opt(request *r, const char *opt, const char *value)
{
  int index = options_find(opt);
  sel_option *s = index >= 0 ? request_add(r, index) : NULL;
  if(!s)
    return;
  /* a new option comes with one empty value */
  if(s->nvalues && !s->values[0][0])
    request_set_value(s, 0, value);
  else
    request_append_value(s, value);
}

static void field_rows(const oas_fields *from, kv_list *rows)
{
  int i;
  for(i = 0; i < from->n; i++) {
    const oas_field *f = &from->f[i];
    char *v = f->value ? NULL : (f->is_file ? NULL : braced(f->name));
    kv_row *row = kv_add(rows, f->name, f->value ? f->value : (v ? v : ""), f->required);
    if(row)
      row->is_file = f->is_file;
    free(v);
  }
}

/* Variables signing in takes: in every environment, empty, for the user to fill. */
static void want_secret(var_store *vars, const char *name, int secret)
{
  char v[VAR_NAME_CAP];
  int i;
  var_name(name, v, sizeof(v));
  for(i = 0; i < vars->n; i++)
    if(!var_find(&vars->e[i], v))
      var_set(&vars->e[i], v, "", secret);
}

static void set_auth(request *r, form_hints *h, const oas_op *o, var_store *vars, char **cookies)
{
  char err[64];
  char *b;
  switch(o->auth) {
  case OAS_AUTH_BASIC:
  case OAS_AUTH_DIGEST:
    form_auth_set(r, h, o->auth == OAS_AUTH_BASIC ? AUTH_BASIC : AUTH_DIGEST, "{{username}}", "{{password}}", NULL, err, sizeof(err));
    want_secret(vars, "username", 0);
    want_secret(vars, "password", 1);
    break;
  case OAS_AUTH_BEARER:
  case OAS_AUTH_OAUTH:
    form_auth_set(r, h, AUTH_BEARER, "{{token}}", NULL, NULL, err, sizeof(err));
    want_secret(vars, "token", 1);
    break;
  case OAS_AUTH_KEY_HEADER:
  case OAS_AUTH_KEY_QUERY:
    if(!o->auth_name)
      break;
    b = braced(o->auth_name);
    form_auth_set(r, h, AUTH_APIKEY, o->auth_name, b, o->auth == OAS_AUTH_KEY_QUERY ? "query" : "header", err, sizeof(err));
    free(b);
    want_secret(vars, o->auth_name, 1);
    break;
  case OAS_AUTH_KEY_COOKIE:
    if(o->auth_name) {
      size_t have = *cookies ? strlen(*cookies) : 0;
      char v[VAR_NAME_CAP];
      char *g;
      var_name(o->auth_name, v, sizeof(v));
      g = realloc(*cookies, have + strlen(o->auth_name) + strlen(v) + 8);
      if(g) {
        sprintf(g + have, "%s%s={{%s}}", have ? "; " : "", o->auth_name, v);
        *cookies = g;
      }
      want_secret(vars, o->auth_name, 1);
    }
    break;
  case OAS_AUTH_MTLS: {
    int cert = options_find("cert"), key = options_find("key");
    sel_option *s = cert >= 0 ? request_add(r, cert) : NULL;
    if(s && !s->nvalues)
      request_add_value(s);
    s = key >= 0 ? request_add(r, key) : NULL;
    if(s && !s->nvalues)
      request_add_value(s);
    break;
  }
  default:
    break;
  }
}

static const char *const g_known_methods[] = { "GET", "POST", "PUT", "PATCH", "DELETE", "HEAD", "OPTIONS", NULL };

static jv *build_request(const oas_op *o, var_store *vars)
{
  request r;
  form_hints h;
  kv_list params, headers, rows;
  char err[128];
  char *path, *url, *built;
  char *cookies = NULL;
  size_t n;
  int i, known = 0;
  jv *out;

  request_init(&r);
  form_hints_init(&h);
  memset(&params, 0, sizeof(params));
  memset(&headers, 0, sizeof(headers));
  memset(&rows, 0, sizeof(rows));

  path = template_vars(o->path);
  n = strlen(path ? path : "") + 16 + (o->raw_query ? strlen(o->raw_query) + 8 : 0);
  url = malloc(n);
  if(url)
    snprintf(url, n, "{{baseUrl}}%s", path ? path : "");
  free(path);
  field_rows(&o->query, &params);
  built = query_build(url ? url : "", &params);
  request_set_url(&r, built ? built : "");
  free(built);
  free(url);
  if(o->raw_query) {
    char *b = braced(o->raw_query);
    size_t len = strlen(r.url) + strlen(b ? b : "") + 2;
    char *with = malloc(len);
    if(with) {
      snprintf(with, len, "%s%s%s", r.url, strchr(r.url, '?') ? "&" : "?", b ? b : "");
      request_set_url(&r, with);
      free(with);
    }
    free(b);
  }
  for(i = 0; i < params.n; i++)
    if(!params.r[i].enabled)
      kv_add(&h.disabled_params, params.r[i].key, params.r[i].value, 0);

  field_rows(&o->headers, &headers);
  /* --json says application/json by itself: any other type is said here */
  if(o->content_type && (o->body == OAS_BODY_RAW || o->body == OAS_BODY_FILE || (o->body == OAS_BODY_JSON && strcmp(o->content_type, "application/json"))))
    kv_add(&headers, "Content-Type", o->content_type, 1);
  form_headers_set(&r, &h, &headers);

  switch(o->body) {
  case OAS_BODY_JSON:
    form_body_set(&r, BODY_JSON, NULL, o->body_text ? o->body_text : "{}", err, sizeof(err));
    break;
  case OAS_BODY_RAW:
    form_body_set(&r, BODY_RAW, NULL, o->body_text ? o->body_text : "", err, sizeof(err));
    break;
  case OAS_BODY_FILE:
    form_body_set(&r, BODY_FILE, NULL, "", err, sizeof(err));
    break;
  case OAS_BODY_URLENC:
  case OAS_BODY_FORM:
    field_rows(&o->body_rows, &rows);
    form_body_set(&r, o->body == OAS_BODY_URLENC ? BODY_URLENC : BODY_FORMDATA, &rows, NULL, err, sizeof(err));
    for(i = 0; i < rows.n; i++) {
      if(!rows.r[i].enabled) {
        kv_row *off = kv_add(&h.disabled_body, rows.r[i].key, rows.r[i].value, 0);
        if(off)
          off->is_file = rows.r[i].is_file;
      }
    }
    break;
  default:
    break;
  }

  for(i = 0; i < o->cookies.n; i++) {
    size_t have = cookies ? strlen(cookies) : 0;
    const oas_field *f = &o->cookies.f[i];
    char *v = f->value ? NULL : braced(f->name);
    const char *text = f->value ? f->value : (v ? v : "");
    char *g = realloc(cookies, have + strlen(f->name) + strlen(text) + 4);
    if(g) {
      sprintf(g + have, "%s%s=%s", have ? "; " : "", f->name, text);
      cookies = g;
    }
    free(v);
  }
  set_auth(&r, &h, o, vars, &cookies);
  if(cookies) {
    add_opt(&r, "cookie", cookies);
    free(cookies);
  }

  for(i = 0; g_known_methods[i]; i++)
    if(!strcmp(o->method, g_known_methods[i]))
      known = 1;
  if(!known)
    add_opt(&r, "request", o->method);
  out = req_to_jv(&r, known ? o->method : "", &h, NULL);

  kv_free(&params);
  kv_free(&headers);
  kv_free(&rows);
  request_free(&r);
  form_hints_free(&h);
  return out;
}

static coll_node *folder_for(coll_node *root, const oas_op *o)
{
  coll_node *at = root;
  int i, k;
  for(i = 0; i < o->nfolders; i++) {
    coll_node *next = NULL;
    for(k = 0; k < at->nkids; k++)
      if(at->kids[k]->kind == NODE_FOLDER && !strcmp(at->kids[k]->name, o->folders[i]))
        next = at->kids[k];
    if(!next)
      next = coll_add(at, NODE_FOLDER, o->folders[i], NULL);
    if(!next)
      return at;
    at = next;
  }
  return at;
}


static char *free_collection_name(const char *title, const char *const *taken, int ntaken)
{
  size_t n = strlen(title) + 16;
  char *name = malloc(n);
  int k = 1, i, clash = 1;
  if(!name)
    return NULL;
  while(clash) {
    if(k == 1)
      snprintf(name, n, "%s", title);
    else
      snprintf(name, n, "%s %d", title, k);
    clash = 0;
    for(i = 0; i < ntaken; i++)
      if(taken && taken[i] && !strcmp(taken[i], name))
        clash = 1;
    k++;
  }
  return name;
}

static void set_up_envs(const oas_api *a, var_store *vars)
{
  int i, k;
  vars_init(vars);
  for(i = 0; i < a->nservers; i++) {
    const oas_server *s = &a->servers[i];
    int e = -1, tries;
    char name[256];
    char *url;
    for(tries = 1; tries < 1000 && e < 0; tries++) {
      if(tries == 1)
        snprintf(name, sizeof(name), "%s", s->name ? s->name : s->url);
      else
        snprintf(name, sizeof(name), "%s %d", s->name ? s->name : s->url, tries);
      if(i == 0)
        e = env_rename(vars, 0, name) ? -1 : 0;
      else
        e = env_add(vars, name);
    }
    if(e < 0)
      continue;
    url = template_vars(s->url);
    var_set(&vars->e[e], "baseUrl", url ? url : "", 0);
    free(url);
    for(k = 0; k < s->vars.n; k++) {
      char v[VAR_NAME_CAP];
      var_name(s->vars.f[k].name, v, sizeof(v));
      var_set(&vars->e[e], v, s->vars.f[k].value ? s->vars.f[k].value : "", 0);
    }
  }
}

int oas_import_file(const char *path, const char *const *taken_names, int ntaken, collection **out, var_store *vars,
                    oas_report *rep, char *err, size_t errn)
{
  jv *root;
  oas_api api;
  collection *c;
  char *name;
  int i;
  memset(rep, 0, sizeof(*rep));
  memset(&api, 0, sizeof(api));
  *out = NULL;
  root = oas_load(path, &rep->load, err, errn);
  if(!root)
    return -1;
  if(oas_read(root, &api, err, errn)) {
    jv_free(root);
    oas_api_free(&api);
    return -1;
  }
  jv_free(root);
  name = free_collection_name(api.title ? api.title : "Imported API", taken_names, ntaken);
  c = name ? coll_new(name) : NULL;
  free(name);
  if(!c) {
    snprintf(err, errn, "out of memory");
    oas_api_free(&api);
    return -1;
  }
  set_up_envs(&api, vars);
  for(i = 0; i < api.nops; i++) {
    const oas_op *o = &api.ops[i];
    jv *req = build_request(o, vars);
    coll_node *parent = folder_for(c->root, o);
    if(req && coll_add(parent, NODE_REQUEST, o->name ? o->name : o->path, req))
      rep->requests++;
  }
  rep->webhooks = api.webhooks;
  rep->callbacks = api.callbacks;
  rep->oauth = api.oauth;
  rep->alternatives_dropped = api.alternatives_dropped;
  rep->xml_bodies = api.xml_bodies;
  oas_api_free(&api);
  *out = c;
  return 0;
}

void oas_report_free(oas_report *r)
{
  oas_load_report_free(&r->load);
}

static size_t skipped(char *out, size_t n, size_t used, int *first, int count, const char *one, const char *many)
{
  if(count <= 0 || used >= n)
    return used;
  used += (size_t)snprintf(out + used, n - used, "%s%d %s", *first ? " Skipped: " : ", ", count, count == 1 ? one : many);
  *first = 0;
  return used;
}

void oas_report_text(const oas_report *r, const char *coll_name, char *out, size_t n)
{
  size_t used = (size_t)snprintf(out, n, "Imported %d request%s into %s.", r->requests, r->requests == 1 ? "" : "s", coll_name);
  int first = 1, i;
  used = skipped(out, n, used, &first, r->webhooks, "webhook", "webhooks");
  used = skipped(out, n, used, &first, r->callbacks, "callback", "callbacks");
  used = skipped(out, n, used, &first, r->load.remote_refs, "remote $ref", "remote $refs");
  used = skipped(out, n, used, &first, r->load.broken_refs, "broken $ref", "broken $refs");
  used = skipped(out, n, used, &first, r->alternatives_dropped, "other way to sign in", "other ways to sign in");
  if(!first && used < n)
    used += (size_t)snprintf(out + used, n - used, ".");
  if(r->oauth && used < n)
    used += (size_t)snprintf(out + used, n - used, " OAuth is not run here: put a token in {{token}}.");
  if(r->xml_bodies && used < n)
    used += (size_t)snprintf(out + used, n - used, " XML bodies start empty.");
  for(i = 0; i < r->load.outside.n && used < n; i++)
    used += (size_t)snprintf(out + used, n - used, "%s%s", i ? ", " : " It also read, from outside its folder: ", r->load.outside.s[i]);
  if(r->load.outside.n && used < n)
    snprintf(out + used, n - used, ".");
}

int oas_looks_like_api(const char *text, size_t len)
{
  jv *v = yaml_parse(text, len, NULL, 0);
  int yes = jv_kind(v) == JV_OBJ && (jv_get(v, "openapi") || jv_get(v, "swagger") || jv_get(v, "swaggerVersion"));
  jv_free(v);
  return yes;
}
