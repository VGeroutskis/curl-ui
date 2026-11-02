/* Swagger 2.0, rewritten as the OpenAPI 3.0 it became, for one reader to do the rest. */
#include "oas_model.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const g_schema_keys[] = { "type", "format", "items", "enum", "default", "minimum", "maximum", "pattern", NULL };

static jv *param_schema(const jv *p)
{
  jv *s = jv_new(JV_OBJ);
  int i;
  for(i = 0; g_schema_keys[i]; i++)
    if(jv_get(p, g_schema_keys[i]))
      jv_set(s, g_schema_keys[i], jv_clone(jv_get(p, g_schema_keys[i])));
  if(jv_get(p, "x-example"))
    jv_set(s, "example", jv_clone(jv_get(p, "x-example")));
  if(jv_str(jv_get(p, "type")) && !strcmp(jv_str(jv_get(p, "type")), "file")) {
    jv_set(s, "type", jv_new_str("string"));
    jv_set(s, "format", jv_new_str("binary"));
  }
  return s;
}

static int in_is(const jv *p, const char *where)
{
  const char *in = jv_str(jv_get(p, "in"));
  return in && !strcmp(in, where);
}

static jv *plain_params(const jv *list)
{
  jv *out = jv_new(JV_ARR);
  int i;
  for(i = 0; i < jv_len(list); i++) {
    const jv *p = jv_at(list, i);
    jv *q;
    if(in_is(p, "body") || in_is(p, "formData"))
      continue;
    q = jv_clone(p);
    jv_set(q, "schema", param_schema(p));
    jv_push(out, q);
  }
  return out;
}

static const char *pick_consumes(const jv *consumes, const char *fallback)
{
  int i;
  for(i = 0; i < jv_len(consumes); i++)
    if(jv_str(jv_at(consumes, i)) && oas_is_json_media(jv_str(jv_at(consumes, i))))
      return jv_str(jv_at(consumes, i));
  return jv_len(consumes) && jv_str(jv_at(consumes, 0)) ? jv_str(jv_at(consumes, 0)) : fallback;
}

static void set_media(jv *op3, const char *media, jv *schema)
{
  jv *body = jv_new(JV_OBJ), *content = jv_new(JV_OBJ), *m = jv_new(JV_OBJ);
  jv_set(m, "schema", schema);
  jv_set(content, media, m);
  jv_set(body, "content", content);
  jv_set(op3, "requestBody", body);
}

/* Body and form parameters, of the path and of the operation, the latter winning. */
static void body_of(const jv *root, const jv *item, const jv *op, jv *op3)
{
  const jv *lists[2];
  const jv *consumes = jv_get(op, "consumes") ? jv_get(op, "consumes") : jv_get(root, "consumes");
  jv *props = jv_new(JV_OBJ), *required = jv_new(JV_ARR);
  const jv *body = NULL;
  int l, i, has_file = 0;
  lists[0] = jv_get(item, "parameters");
  lists[1] = jv_get(op, "parameters");
  for(l = 0; l < 2; l++) {
    for(i = 0; i < jv_len(lists[l]); i++) {
      const jv *p = jv_at(lists[l], i);
      if(in_is(p, "body"))
        body = p;
      else if(in_is(p, "formData") && jv_str(jv_get(p, "name"))) {
        const char *t = jv_str(jv_get(p, "type"));
        jv_set(props, jv_str(jv_get(p, "name")), param_schema(p));
        if(t && !strcmp(t, "file"))
          has_file = 1;
        if(jv_bool(jv_get(p, "required")))
          jv_push(required, jv_new_str(jv_str(jv_get(p, "name"))));
      }
    }
  }
  if(body) {
    const jv *schema = jv_get(body, "schema");
    set_media(op3, pick_consumes(consumes, "application/json"), schema ? jv_clone(schema) : jv_new(JV_OBJ));
  }
  else if(jv_len(props)) {
    jv *schema = jv_new(JV_OBJ);
    int multi = has_file;
    for(i = 0; i < jv_len(consumes); i++)
      if(jv_str(jv_at(consumes, i)) && !strncmp(jv_str(jv_at(consumes, i)), "multipart/", 10))
        multi = 1;
    jv_set(schema, "type", jv_new_str("object"));
    jv_set(schema, "properties", props);
    jv_set(schema, "required", required);
    props = required = NULL;
    set_media(op3, multi ? "multipart/form-data" : "application/x-www-form-urlencoded", schema);
  }
  jv_free(props);
  jv_free(required);
}

static jv *convert_paths(const jv *root)
{
  static const char *const methods[] = { "get", "put", "post", "delete", "options", "head", "patch" };
  const jv *paths = jv_get(root, "paths");
  jv *out = jv_new(JV_OBJ);
  int i;
  for(i = 0; i < jv_len(paths); i++) {
    const char *path = jv_key_at(paths, i);
    const jv *item = jv_get(paths, path);
    jv *item3 = jv_new(JV_OBJ);
    size_t m;
    jv_set(item3, "parameters", plain_params(jv_get(item, "parameters")));
    for(m = 0; m < sizeof(methods) / sizeof(methods[0]); m++) {
      const jv *op = jv_get(item, methods[m]);
      jv *op3;
      if(!op)
        continue;
      op3 = jv_clone(op);
      jv_set(op3, "parameters", plain_params(jv_get(op, "parameters")));
      body_of(root, item, op, op3);
      jv_set(item3, methods[m], op3);
    }
    jv_set(out, path, item3);
  }
  return out;
}

static jv *convert_schemes(const jv *defs)
{
  jv *out = jv_new(JV_OBJ);
  int i;
  for(i = 0; i < jv_len(defs); i++) {
    const char *k = jv_key_at(defs, i);
    const jv *d = jv_get(defs, k);
    const char *t = jv_str(jv_get(d, "type"));
    if(t && !strcmp(t, "basic")) {
      jv *s = jv_new(JV_OBJ);
      jv_set(s, "type", jv_new_str("http"));
      jv_set(s, "scheme", jv_new_str("basic"));
      jv_set(out, k, s);
    }
    else
      jv_set(out, k, jv_clone(d));
  }
  return out;
}

static jv *convert_servers(const jv *root)
{
  const char *host = jv_str(jv_get(root, "host")), *base = jv_str(jv_get(root, "basePath"));
  const jv *schemes = jv_get(root, "schemes");
  jv *out = jv_new(JV_ARR);
  char url[2048];
  int i;
  if(host) {
    int n = jv_len(schemes);
    for(i = 0; i < (n ? n : 1); i++) {
      jv *s = jv_new(JV_OBJ);
      const char *scheme = n ? jv_str(jv_at(schemes, i)) : "https";
      snprintf(url, sizeof(url), "%s://%s%s", scheme ? scheme : "https", host, base ? base : "");
      jv_set(s, "url", jv_new_str(url));
      jv_push(out, s);
    }
  }
  else if(base) {
    jv *s = jv_new(JV_OBJ);
    jv_set(s, "url", jv_new_str(base));
    jv_push(out, s);
  }
  return out;
}

int oas_read2(const jv *root, oas_api *out, char *err, size_t errn)
{
  jv *v3 = jv_new(JV_OBJ), *components = jv_new(JV_OBJ);
  int rc;
  static const char *const copied[] = { "info", "tags", "security", NULL };
  int i;
  if(!jv_get(root, "paths")) {
    jv_free(v3);
    jv_free(components);
    snprintf(err, errn, "This API description has no paths, so it may be cut short");
    return -1;
  }
  for(i = 0; copied[i]; i++)
    if(jv_get(root, copied[i]))
      jv_set(v3, copied[i], jv_clone(jv_get(root, copied[i])));
  jv_set(v3, "servers", convert_servers(root));
  jv_set(v3, "paths", convert_paths(root));
  jv_set(components, "securitySchemes", convert_schemes(jv_get(root, "securityDefinitions")));
  jv_set(v3, "components", components);
  rc = oas_read3(v3, out, err, errn);
  jv_free(v3);
  return rc;
}
