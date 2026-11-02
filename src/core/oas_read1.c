/* Swagger 1.x, rewritten as OpenAPI 3.0. 1.2 has a published shape; 1.0 and 1.1 are taken where they
   have the same one, with httpMethod, dataType, responseClass and allowableValues in their old names. */
#include "oas_model.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MODEL_MAX_DEPTH 12

static jv *type_schema(const char *type, const jv *models, const jv *from, int depth);

static jv *model_schema(const jv *m, const jv *models, int depth)
{
  const jv *props = jv_get(m, "properties");
  jv *out = jv_new(JV_OBJ), *p3 = jv_new(JV_OBJ);
  int i;
  jv_set(out, "type", jv_new_str("object"));
  for(i = 0; i < jv_len(props); i++) {
    const char *k = jv_key_at(props, i);
    const jv *p = jv_get(props, k);
    const char *ref = jv_str(jv_get(p, "$ref"));
    const char *t = jv_str(jv_get(p, "type"));
    if(!t)
      t = jv_str(jv_get(p, "dataType"));
    jv_set(p3, k, type_schema(ref ? ref : t, models, p, depth + 1));
  }
  jv_set(out, "properties", p3);
  return out;
}

/* A type by its 1.x name: a primitive, a container or a model. */
static jv *type_schema(const char *type, const jv *models, const jv *from, int depth)
{
  jv *s = jv_new(JV_OBJ);
  char lower[32] = "";
  size_t i;
  const jv *v;
  if(depth > MODEL_MAX_DEPTH || !type)
    return s;
  for(i = 0; type[i] && i + 1 < sizeof(lower); i++)
    lower[i] = (char)tolower((unsigned char)type[i]);
  lower[i] = '\0';
  if(jv_get(models, type)) {
    jv_free(s);
    return model_schema(jv_get(models, type), models, depth);
  }
  if(!strcmp(lower, "string") || !strcmp(lower, "date") || !strcmp(lower, "date-time"))
    jv_set(s, "type", jv_new_str("string"));
  else if(!strcmp(lower, "file")) {
    jv_set(s, "type", jv_new_str("string"));
    jv_set(s, "format", jv_new_str("binary"));
  }
  else if(!strcmp(lower, "integer") || !strcmp(lower, "int") || !strcmp(lower, "long") || !strcmp(lower, "number") ||
          !strcmp(lower, "float") || !strcmp(lower, "double") || !strcmp(lower, "byte"))
    jv_set(s, "type", jv_new_str("integer"));
  else if(!strcmp(lower, "boolean"))
    jv_set(s, "type", jv_new_str("boolean"));
  else if(!strcmp(lower, "array") || !strcmp(lower, "list") || !strcmp(lower, "set")) {
    const jv *items = jv_get(from, "items");
    const char *it = jv_str(jv_get(items, "$ref"));
    if(!it)
      it = jv_str(jv_get(items, "type"));
    jv_set(s, "type", jv_new_str("array"));
    jv_set(s, "items", type_schema(it, models, items, depth + 1));
  }
  if((v = jv_get(from, "defaultValue")) != NULL)
    jv_set(s, "default", jv_clone(v));
  if((v = jv_get(from, "enum")) != NULL)
    jv_set(s, "enum", jv_clone(v));
  else if((v = jv_get(jv_get(from, "allowableValues"), "values")) != NULL)
    jv_set(s, "enum", jv_clone(v));
  return s;
}

static jv *param_schema(const jv *p, const jv *models)
{
  const char *t = jv_str(jv_get(p, "type"));
  if(!t)
    t = jv_str(jv_get(p, "dataType"));
  return type_schema(t, models, p, 0);
}

static void set_media(jv *op3, const char *media, jv *schema)
{
  jv *body = jv_new(JV_OBJ), *content = jv_new(JV_OBJ), *m = jv_new(JV_OBJ);
  jv_set(m, "schema", schema);
  jv_set(content, media, m);
  jv_set(body, "content", content);
  jv_set(op3, "requestBody", body);
}

static const char *pick_consumes(const jv *consumes, const char *fallback)
{
  return jv_len(consumes) && jv_str(jv_at(consumes, 0)) ? jv_str(jv_at(consumes, 0)) : fallback;
}

static jv *convert_op(const jv *op, const jv *decl, const char *tag)
{
  const jv *models = jv_get(decl, "models"), *params = jv_get(op, "parameters");
  const jv *consumes = jv_get(op, "consumes") ? jv_get(op, "consumes") : jv_get(decl, "consumes");
  const jv *auth = jv_get(op, "authorizations");
  jv *o = jv_new(JV_OBJ), *list = jv_new(JV_ARR), *tags = jv_new(JV_ARR);
  jv *form = NULL, *required = jv_new(JV_ARR);
  const jv *body = NULL;
  int i, has_file = 0;
  if(jv_get(op, "summary"))
    jv_set(o, "summary", jv_clone(jv_get(op, "summary")));
  if(jv_get(op, "nickname"))
    jv_set(o, "operationId", jv_clone(jv_get(op, "nickname")));
  jv_push(tags, jv_new_str(tag));
  jv_set(o, "tags", tags);
  for(i = 0; i < jv_len(params); i++) {
    const jv *p = jv_at(params, i);
    const char *where = jv_str(jv_get(p, "paramType")), *name = jv_str(jv_get(p, "name"));
    if(!where)
      continue;
    if(!strcmp(where, "body"))
      body = p;
    else if(!strcmp(where, "form") && name) {
      const char *t = jv_str(jv_get(p, "type"));
      if(!t)
        t = jv_str(jv_get(p, "dataType"));
      if(!form)
        form = jv_new(JV_OBJ);
      jv_set(form, name, param_schema(p, models));
      if(t && (!strcmp(t, "File") || !strcmp(t, "file")))
        has_file = 1;
      if(jv_bool(jv_get(p, "required")))
        jv_push(required, jv_new_str(name));
    }
    else if(name) {
      jv *q = jv_new(JV_OBJ);
      jv_set(q, "name", jv_new_str(name));
      jv_set(q, "in", jv_new_str(where));
      jv_set(q, "required", jv_new_bool(jv_bool(jv_get(p, "required"))));
      jv_set(q, "schema", param_schema(p, models));
      jv_push(list, q);
    }
  }
  jv_set(o, "parameters", list);
  if(body)
    set_media(o, pick_consumes(consumes, "application/json"), param_schema(body, models));
  else if(form) {
    jv *schema = jv_new(JV_OBJ);
    jv_set(schema, "type", jv_new_str("object"));
    jv_set(schema, "properties", form);
    jv_set(schema, "required", required);
    required = NULL;
    form = NULL;
    set_media(o, has_file ? "multipart/form-data" : pick_consumes(consumes, "application/x-www-form-urlencoded"), schema);
  }
  jv_free(form);
  jv_free(required);
  if(jv_len(auth)) {
    jv *sec = jv_new(JV_ARR);
    for(i = 0; i < jv_len(auth); i++) {
      jv *alt = jv_new(JV_OBJ);
      jv_set(alt, jv_key_at(auth, i), jv_new(JV_ARR));
      jv_push(sec, alt);
    }
    jv_set(o, "security", sec);
  }
  return o;
}

static void convert_decl(const jv *decl, jv *paths)
{
  const jv *apis = jv_get(decl, "apis");
  const char *resource = jv_str(jv_get(decl, "resourcePath"));
  char tag[256];
  int i, k;
  snprintf(tag, sizeof(tag), "%s", resource ? resource + strspn(resource, "/") : "");
  { /* a resource can be named with its format */
    char *dot = strstr(tag, ".{format}");
    if(dot)
      *dot = '\0';
  }
  for(i = 0; i < jv_len(apis); i++) {
    const jv *api = jv_at(apis, i), *ops = jv_get(api, "operations");
    const char *raw = jv_str(jv_get(api, "path"));
    char path[1024];
    char *fmt;
    jv *item;
    if(!raw)
      continue;
    snprintf(path, sizeof(path), "%s%s", raw[0] == '/' ? "" : "/", raw);
    /* {format} is in the path only to name the response format: JSON will do */
    while((fmt = strstr(path, "{format}")) != NULL) {
      memcpy(fmt, "json", 4);
      memmove(fmt + 4, fmt + 8, strlen(fmt + 8) + 1);
    }
    item = jv_get(paths, path) ? jv_clone(jv_get(paths, path)) : jv_new(JV_OBJ);
    for(k = 0; k < jv_len(ops); k++) {
      const jv *op = jv_at(ops, k);
      const char *m = jv_str(jv_get(op, "method"));
      char lower[16] = "";
      size_t c;
      if(!m)
        m = jv_str(jv_get(op, "httpMethod"));
      if(!m)
        continue;
      for(c = 0; m[c] && c + 1 < sizeof(lower); c++)
        lower[c] = (char)tolower((unsigned char)m[c]);
      lower[c] = '\0';
      jv_set(item, lower, convert_op(op, decl, tag[0] ? tag : "default"));
    }
    jv_set(paths, path, item);
  }
}

static jv *convert_schemes(const jv *auths)
{
  jv *out = jv_new(JV_OBJ);
  int i;
  for(i = 0; i < jv_len(auths); i++) {
    const char *k = jv_key_at(auths, i);
    const jv *a = jv_get(auths, k);
    const char *t = jv_str(jv_get(a, "type"));
    jv *s = jv_new(JV_OBJ);
    if(t && !strcmp(t, "basicAuth")) {
      jv_set(s, "type", jv_new_str("http"));
      jv_set(s, "scheme", jv_new_str("basic"));
    }
    else if(t && !strcmp(t, "apiKey")) {
      jv_set(s, "type", jv_new_str("apiKey"));
      if(jv_get(a, "passAs"))
        jv_set(s, "in", jv_clone(jv_get(a, "passAs")));
      if(jv_get(a, "keyname"))
        jv_set(s, "name", jv_clone(jv_get(a, "keyname")));
    }
    else if(t)
      jv_set(s, "type", jv_new_str("oauth2"));
    jv_set(out, k, s);
  }
  return out;
}

static int has_operations(const jv *v)
{
  const jv *apis = jv_get(v, "apis");
  int i;
  for(i = 0; i < jv_len(apis); i++)
    if(jv_get(jv_at(apis, i), "operations"))
      return 1;
  return 0;
}

int oas_read1(const jv *root, oas_api *out, char *err, size_t errn)
{
  const jv *decls = jv_get(root, "_declarations");
  const jv *version = jv_get(root, "swaggerVersion");
  const char *ver = jv_str(version);
  jv *v3, *info, *paths, *components, *servers;
  const char *base;
  int i, rc;

  if(!ver || ver[0] != '1' || ver[1] != '.' || !strchr("012", ver[2]) || ver[3]) {
    snprintf(err, errn, "Swagger version %s is not one this app knows", ver ? ver : "(not text)");
    return -1;
  }
  if(!has_operations(root) && !jv_len(decls)) {
    char keys[256] = "";
    size_t used = 0;
    for(i = 0; i < jv_len(root) && used < sizeof(keys); i++)
      used += (size_t)snprintf(keys + used, sizeof(keys) - used, "%s%s", i ? ", " : "", jv_key_at(root, i));
    snprintf(err, errn, "This Swagger %s file has no API this app can read. It has: %s", ver, keys);
    return -1;
  }
  v3 = jv_new(JV_OBJ);
  info = jv_new(JV_OBJ);
  paths = jv_new(JV_OBJ);
  components = jv_new(JV_OBJ);
  servers = jv_new(JV_ARR);
  if(jv_get(jv_get(root, "info"), "title"))
    jv_set(info, "title", jv_clone(jv_get(jv_get(root, "info"), "title")));
  jv_set(v3, "info", info);
  base = jv_str(jv_get(root, "basePath"));
  if(has_operations(root)) {
    convert_decl(root, paths);
  }
  for(i = 0; i < jv_len(decls); i++) {
    const jv *d = jv_at(decls, i);
    if(!base)
      base = jv_str(jv_get(d, "basePath"));
    convert_decl(d, paths);
  }
  if(base) {
    jv *s = jv_new(JV_OBJ);
    jv_set(s, "url", jv_new_str(base));
    jv_push(servers, s);
  }
  jv_set(v3, "servers", servers);
  jv_set(v3, "paths", paths);
  jv_set(components, "securitySchemes", convert_schemes(jv_get(root, "authorizations")));
  jv_set(v3, "components", components);
  rc = oas_read3(v3, out, err, errn);
  jv_free(v3);
  return rc;
}
