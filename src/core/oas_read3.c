/* OpenAPI 3.0, 3.1 and 3.2. What they differ in is mostly inside schemas, which oas_model.c reads. */
#include "oas_model.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const jv *first_example(const jv *examples)
{
  const jv *e;
  if(jv_kind(examples) != JV_OBJ || !jv_len(examples))
    return NULL;
  e = jv_get(examples, jv_key_at(examples, 0));
  if(jv_get(e, "value"))
    return jv_get(e, "value");
  if(jv_get(e, "dataValue"))
    return jv_get(e, "dataValue");
  return jv_get(e, "serializedValue");
}

/* An example of the thing itself, or of its media type. */
static const jv *given_example(const jv *holder)
{
  const jv *v = jv_get(holder, "example");
  return v ? v : first_example(jv_get(holder, "examples"));
}

static char *param_value(const jv *p)
{
  const jv *v = given_example(p);
  const jv *content = jv_get(p, "content");
  if(!v)
    v = oas_schema_value(jv_get(p, "schema"));
  if(!v && jv_len(content)) {
    const jv *m = jv_get(content, jv_key_at(content, 0));
    v = given_example(m);
    if(!v)
      v = oas_schema_value(jv_get(m, "schema"));
  }
  return oas_scalar_text(v);
}

static void add_param(oas_op *o, const jv *p)
{
  const char *name = jv_str(jv_get(p, "name")), *in = jv_str(jv_get(p, "in"));
  int required = jv_bool(jv_get(p, "required"));
  char *value;
  oas_fields *to = NULL;
  if(!in)
    return;
  if(!strcmp(in, "querystring")) {
    free(o->raw_query);
    o->raw_query = oas_strdup(name ? name : "query");
    return;
  }
  if(!name)
    return;
  if(!strcmp(in, "query"))
    to = &o->query;
  else if(!strcmp(in, "header") && !oas_is_ignored_header(name))
    to = &o->headers;
  else if(!strcmp(in, "cookie"))
    to = &o->cookies;
  if(!to)
    return;
  value = param_value(p);
  oas_add_field(to, name, value, required);
  free(value);
}

/* Parameters of the operation win over those of the path, by name and place. */
static void add_params(oas_op *o, const jv *path_item, const jv *operation)
{
  const jv *common = jv_get(path_item, "parameters"), *own = jv_get(operation, "parameters");
  int i, k;
  for(i = 0; i < jv_len(common); i++) {
    const jv *p = jv_at(common, i);
    int replaced = 0;
    for(k = 0; k < jv_len(own); k++) {
      const char *n1 = jv_str(jv_get(p, "name")), *n2 = jv_str(jv_get(jv_at(own, k), "name"));
      const char *w1 = jv_str(jv_get(p, "in")), *w2 = jv_str(jv_get(jv_at(own, k), "in"));
      if(n1 && n2 && w1 && w2 && !strcmp(n1, n2) && !strcmp(w1, w2))
        replaced = 1;
    }
    if(!replaced)
      add_param(o, p);
  }
  for(k = 0; k < jv_len(own); k++)
    add_param(o, jv_at(own, k));
}

static const char *pick_media(const jv *content, int (*test)(const char *))
{
  int i;
  for(i = 0; i < jv_len(content); i++)
    if(test(jv_key_at(content, i)))
      return jv_key_at(content, i);
  return NULL;
}

static int is_urlenc(const char *m)
{
  return !strncmp(m, "application/x-www-form-urlencoded", 33);
}

static int is_multipart(const char *m)
{
  return !strncmp(m, "multipart/", 10);
}

static void set_body(oas_api *a, oas_op *o, const jv *body)
{
  const jv *content = jv_get(body, "content");
  const char *m;
  const jv *media, *ex;
  if(!jv_len(content))
    return;
  if((m = pick_media(content, oas_is_json_media)) != NULL) {
    media = jv_get(content, m);
    ex = given_example(media);
    o->body = OAS_BODY_JSON;
    if(jv_kind(ex) == JV_STR)
      o->body_text = oas_strdup(jv_str(ex));
    else if(ex)
      o->body_text = jv_write(ex, 1, NULL);
    else if(jv_get(media, "schema"))
      o->body_text = oas_sample(jv_get(media, "schema"));
    else
      o->body_text = oas_strdup("{}");
  }
  else if((m = pick_media(content, is_urlenc)) != NULL || (m = pick_media(content, is_multipart)) != NULL) {
    media = jv_get(content, m);
    o->body = is_urlenc(m) ? OAS_BODY_URLENC : OAS_BODY_FORM;
    oas_rows_from_schema(jv_get(media, "schema"), given_example(media), &o->body_rows);
  }
  else if((m = pick_media(content, oas_is_text_media)) != NULL) {
    media = jv_get(content, m);
    ex = given_example(media);
    if(!ex)
      ex = oas_schema_value(jv_get(media, "schema"));
    o->body = OAS_BODY_RAW;
    o->body_text = oas_scalar_text(ex);
    if(!o->body_text)
      o->body_text = oas_strdup("");
    if(oas_is_xml_media(m) && !ex)
      a->xml_bodies++;
  }
  else {
    m = jv_key_at(content, 0);
    if(!strcmp(m, "*/*")) {
      o->body = OAS_BODY_RAW;
      o->body_text = oas_strdup("");
      return;
    }
    o->body = OAS_BODY_FILE;
  }
  o->content_type = oas_strdup(m);
}

static int ci_eq(const char *a, const char *b)
{
  for(; *a && *b; a++, b++)
    if(tolower((unsigned char)*a) != tolower((unsigned char)*b))
      return 0;
  return !*a && !*b;
}

static void set_auth(oas_api *a, oas_op *o, const jv *root, const jv *operation)
{
  const jv *sec = jv_get(operation, "security");
  const jv *schemes = jv_get(jv_get(root, "components"), "securitySchemes");
  const jv *chosen = NULL, *s;
  const char *type, *name;
  int i, usable = 0;
  if(!sec)
    sec = jv_get(root, "security");
  /* {} says anonymous may do: the first with a scheme is still what is offered */
  for(i = 0; i < jv_len(sec); i++) {
    if(jv_len(jv_at(sec, i))) {
      usable++;
      if(!chosen)
        chosen = jv_at(sec, i);
    }
  }
  if(!chosen)
    return;
  if(usable > 1 || jv_len(chosen) > 1)
    a->alternatives_dropped++;
  name = jv_key_at(chosen, 0);
  s = jv_get(schemes, name);
  type = jv_str(jv_get(s, "type"));
  if(!type)
    return;
  if(!strcmp(type, "http")) {
    const char *sc = jv_str(jv_get(s, "scheme"));
    if(sc && ci_eq(sc, "basic"))
      o->auth = OAS_AUTH_BASIC;
    else if(sc && ci_eq(sc, "bearer"))
      o->auth = OAS_AUTH_BEARER;
    else if(sc && ci_eq(sc, "digest"))
      o->auth = OAS_AUTH_DIGEST;
  }
  else if(!strcmp(type, "apiKey")) {
    const char *in = jv_str(jv_get(s, "in"));
    if(in && !strcmp(in, "header"))
      o->auth = OAS_AUTH_KEY_HEADER;
    else if(in && !strcmp(in, "query"))
      o->auth = OAS_AUTH_KEY_QUERY;
    else if(in && !strcmp(in, "cookie"))
      o->auth = OAS_AUTH_KEY_COOKIE;
    o->auth_name = oas_strdup(jv_str(jv_get(s, "name")));
  }
  else if(!strcmp(type, "oauth2") || !strcmp(type, "openIdConnect")) {
    o->auth = OAS_AUTH_OAUTH;
    a->oauth++;
  }
  else if(!strcmp(type, "mutualTLS"))
    o->auth = OAS_AUTH_MTLS;
}

/* The first tag, under the tags it names as parents. */
static void set_folders(oas_op *o, const jv *root, const jv *operation)
{
  const char *chain[16];
  int n = 0, i, k;
  const char *tag = jv_str(jv_at(jv_get(operation, "tags"), 0));
  const jv *tags = jv_get(root, "tags");
  while(tag && n < 16) {
    const char *parent = NULL;
    int seen = 0;
    for(k = 0; k < n; k++)
      if(!strcmp(chain[k], tag))
        seen = 1;
    if(seen)
      break;
    chain[n++] = tag;
    for(i = 0; i < jv_len(tags); i++)
      if(jv_str(jv_get(jv_at(tags, i), "name")) && !strcmp(jv_str(jv_get(jv_at(tags, i), "name")), tag))
        parent = jv_str(jv_get(jv_at(tags, i), "parent"));
    tag = parent;
  }
  for(i = n - 1; i >= 0; i--)
    oas_add_folder(o, chain[i]);
}

static void add_operation(oas_api *a, const jv *root, const jv *item, const char *method, const char *path, const jv *operation)
{
  oas_op *o = oas_add_op(a, method, path);
  const char *name;
  if(!o)
    return;
  name = jv_str(jv_get(operation, "summary"));
  if(!name || !*name)
    name = jv_str(jv_get(operation, "operationId"));
  if(name && *name)
    o->name = oas_strdup(name);
  else {
    size_t n = strlen(o->method) + strlen(path) + 2;
    o->name = malloc(n);
    if(o->name)
      snprintf(o->name, n, "%s %s", o->method, path);
  }
  set_folders(o, root, operation);
  add_params(o, item, operation);
  set_body(a, o, jv_get(operation, "requestBody"));
  set_auth(a, o, root, operation);
  a->callbacks += jv_len(jv_get(operation, "callbacks"));
}

int oas_read3(const jv *root, oas_api *out, char *err, size_t errn)
{
  static const char *const methods[] = { "get", "put", "post", "delete", "options", "head", "patch", "trace", "query" };
  const jv *paths = jv_get(root, "paths"), *servers = jv_get(root, "servers");
  int i, k;
  size_t m;
  if(!paths && !jv_get(root, "webhooks")) {
    snprintf(err, errn, "This API description has no paths, so it may be cut short");
    return -1;
  }
  out->title = oas_strdup(jv_str(jv_get(jv_get(root, "info"), "title")));
  if(!out->title)
    out->title = oas_strdup("Imported API");
  for(i = 0; i < jv_len(servers); i++) {
    const jv *s = jv_at(servers, i), *vars = jv_get(s, "variables");
    const char *url = jv_str(jv_get(s, "url")), *name = jv_str(jv_get(s, "name"));
    oas_server *srv;
    if(!url)
      continue;
    if(!name)
      name = jv_str(jv_get(s, "description"));
    srv = oas_add_server(out, name ? name : url, url);
    for(k = 0; srv && k < jv_len(vars); k++) {
      const char *vn = jv_key_at(vars, k);
      char *def = oas_scalar_text(jv_get(jv_get(vars, vn), "default"));
      oas_add_field(&srv->vars, vn, def, 1);
      free(def);
    }
  }
  for(i = 0; i < jv_len(paths); i++) {
    const char *path = jv_key_at(paths, i);
    const jv *item = jv_get(paths, path), *extra = jv_get(item, "additionalOperations");
    if(jv_kind(item) != JV_OBJ || path[0] != '/')
      continue;
    /* in the order of the file */
    for(k = 0; k < jv_len(item); k++) {
      const char *key = jv_key_at(item, k);
      for(m = 0; m < sizeof(methods) / sizeof(methods[0]); m++)
        if(!strcmp(key, methods[m]))
          add_operation(out, root, item, key, path, jv_get(item, key));
    }
    for(k = 0; k < jv_len(extra); k++)
      add_operation(out, root, item, jv_key_at(extra, k), path, jv_get(extra, jv_key_at(extra, k)));
  }
  out->webhooks = jv_len(jv_get(root, "webhooks"));
  return 0;
}
