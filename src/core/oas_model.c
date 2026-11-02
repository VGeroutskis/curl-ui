#include "oas_model.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SAMPLE_MAX_DEPTH 12

char *oas_strdup(const char *s)
{
  char *c;
  size_t n;
  if(!s)
    return NULL;
  n = strlen(s) + 1;
  c = malloc(n);
  if(c)
    memcpy(c, s, n);
  return c;
}

static void fields_free(oas_fields *l)
{
  int i;
  for(i = 0; i < l->n; i++) {
    free(l->f[i].name);
    free(l->f[i].value);
  }
  free(l->f);
  l->f = NULL;
  l->n = 0;
}

void oas_api_free(oas_api *a)
{
  int i, k;
  for(i = 0; i < a->nservers; i++) {
    free(a->servers[i].name);
    free(a->servers[i].url);
    fields_free(&a->servers[i].vars);
  }
  for(i = 0; i < a->nops; i++) {
    oas_op *o = &a->ops[i];
    free(o->path);
    free(o->name);
    for(k = 0; k < o->nfolders; k++)
      free(o->folders[k]);
    free(o->folders);
    fields_free(&o->query);
    fields_free(&o->headers);
    fields_free(&o->cookies);
    fields_free(&o->body_rows);
    free(o->raw_query);
    free(o->body_text);
    free(o->content_type);
    free(o->auth_name);
  }
  free(a->servers);
  free(a->ops);
  free(a->title);
  memset(a, 0, sizeof(*a));
}

oas_op *oas_add_op(oas_api *a, const char *method, const char *path)
{
  oas_op *g = realloc(a->ops, sizeof(oas_op) * (size_t)(a->nops + 1)), *o;
  size_t i;
  if(!g)
    return NULL;
  a->ops = g;
  o = &a->ops[a->nops++];
  memset(o, 0, sizeof(*o));
  for(i = 0; method[i] && i + 1 < sizeof(o->method); i++)
    o->method[i] = (char)toupper((unsigned char)method[i]);
  o->path = oas_strdup(path);
  return o;
}

oas_server *oas_add_server(oas_api *a, const char *name, const char *url)
{
  oas_server *g = realloc(a->servers, sizeof(oas_server) * (size_t)(a->nservers + 1)), *s;
  if(!g)
    return NULL;
  a->servers = g;
  s = &a->servers[a->nservers++];
  memset(s, 0, sizeof(*s));
  s->name = oas_strdup(name);
  s->url = oas_strdup(url);
  return s;
}

oas_field *oas_add_field(oas_fields *l, const char *name, const char *value, int required)
{
  oas_field *g = realloc(l->f, sizeof(oas_field) * (size_t)(l->n + 1)), *f;
  if(!g)
    return NULL;
  l->f = g;
  f = &l->f[l->n++];
  memset(f, 0, sizeof(*f));
  f->name = oas_strdup(name);
  f->value = oas_strdup(value);
  f->required = required;
  return f;
}

void oas_add_folder(oas_op *op, const char *name)
{
  char **g = realloc(op->folders, sizeof(char *) * (size_t)(op->nfolders + 1));
  if(g) {
    op->folders = g;
    op->folders[op->nfolders++] = oas_strdup(name);
  }
}

char *oas_scalar_text(const jv *v)
{
  char tmp[64];
  switch(jv_kind(v)) {
  case JV_STR:
    return oas_strdup(jv_str(v));
  case JV_NUM:
    snprintf(tmp, sizeof(tmp), "%.15g", jv_num(v));
    return oas_strdup(tmp);
  case JV_BOOL:
    return oas_strdup(jv_bool(v) ? "true" : "false");
  case JV_ARR:
  case JV_OBJ:
    return jv_write(v, 0, NULL);
  default:
    return NULL;
  }
}

const jv *oas_schema_value(const jv *schema)
{
  const jv *v;
  if(jv_kind(schema) != JV_OBJ)
    return NULL;
  if((v = jv_get(schema, "example")) != NULL)
    return v;
  if((v = jv_get(schema, "default")) != NULL)
    return v;
  if((v = jv_get(schema, "const")) != NULL)
    return v;
  if(jv_len(jv_get(schema, "enum")))
    return jv_at(jv_get(schema, "enum"), 0);
  if(jv_kind(jv_get(schema, "examples")) == JV_ARR && jv_len(jv_get(schema, "examples")))
    return jv_at(jv_get(schema, "examples"), 0);
  return NULL;
}

static int ci_eq(const char *a, const char *b)
{
  for(; *a && *b; a++, b++)
    if(tolower((unsigned char)*a) != tolower((unsigned char)*b))
      return 0;
  return !*a && !*b;
}

int oas_is_ignored_header(const char *name)
{
  return ci_eq(name, "Accept") || ci_eq(name, "Content-Type") || ci_eq(name, "Authorization");
}

static int media_ends(const char *m, const char *suffix)
{
  size_t n = strcspn(m, ";"), s = strlen(suffix);
  return n >= s && !strncmp(m + n - s, suffix, s);
}

int oas_is_json_media(const char *m)
{
  return !strncmp(m, "application/json", 16) || media_ends(m, "+json");
}

int oas_is_xml_media(const char *m)
{
  return !strncmp(m, "application/xml", 15) || !strncmp(m, "text/xml", 8) || media_ends(m, "+xml");
}

int oas_is_text_media(const char *m)
{
  return !strncmp(m, "text/", 5) || oas_is_xml_media(m) || !strncmp(m, "application/yaml", 16) || !strncmp(m, "application/x-yaml", 18);
}

static const char *schema_type(const jv *schema)
{
  const jv *t = jv_get(schema, "type");
  int i;
  if(jv_kind(t) == JV_STR)
    return jv_str(t);
  for(i = 0; i < jv_len(t); i++)
    if(jv_str(jv_at(t, i)) && strcmp(jv_str(jv_at(t, i)), "null"))
      return jv_str(jv_at(t, i));
  return NULL;
}

static jv *sample_jv(const jv *s, int depth)
{
  const jv *v;
  const char *type;
  int i;
  if(depth >= SAMPLE_MAX_DEPTH)
    return jv_new(JV_NULL);
  if(jv_kind(s) != JV_OBJ)
    return jv_new(JV_OBJ);
  if((v = oas_schema_value(s)) != NULL)
    return jv_clone(v);
  if(jv_len(jv_get(s, "oneOf")))
    return sample_jv(jv_at(jv_get(s, "oneOf"), 0), depth);
  if(jv_len(jv_get(s, "anyOf")))
    return sample_jv(jv_at(jv_get(s, "anyOf"), 0), depth);
  type = schema_type(s);
  if(jv_len(jv_get(s, "allOf")) || jv_get(s, "properties") || (type && !strcmp(type, "object"))) {
    jv *obj = jv_new(JV_OBJ);
    const jv *all = jv_get(s, "allOf"), *props = jv_get(s, "properties");
    for(i = 0; i < jv_len(all); i++) {
      jv *part = sample_jv(jv_at(all, i), depth);
      if(jv_kind(part) == JV_OBJ) {
        int k;
        for(k = 0; k < jv_len(part); k++)
          jv_set(obj, jv_key_at(part, k), jv_clone(jv_get(part, jv_key_at(part, k))));
        jv_free(part);
      }
      else if(!jv_len(obj) && i == jv_len(all) - 1 && !props) {
        jv_free(obj);
        return part;
      }
      else
        jv_free(part);
    }
    for(i = 0; i < jv_len(props); i++) {
      const char *k = jv_key_at(props, i);
      jv_set(obj, k, sample_jv(jv_get(props, k), depth + 1));
    }
    return obj;
  }
  if((type && !strcmp(type, "array")) || jv_get(s, "items")) {
    jv *arr = jv_new(JV_ARR);
    if(jv_get(s, "items"))
      jv_push(arr, sample_jv(jv_get(s, "items"), depth + 1));
    return arr;
  }
  if(type && !strcmp(type, "string"))
    return jv_new_str("string");
  if(type && (!strcmp(type, "integer") || !strcmp(type, "number")))
    return jv_new_num(0);
  if(type && !strcmp(type, "boolean"))
    return jv_new_bool(0);
  if(type && !strcmp(type, "null"))
    return jv_new(JV_NULL);
  return jv_new(JV_OBJ);
}

char *oas_sample(const jv *schema)
{
  jv *v = sample_jv(schema, 0);
  char *t = v ? jv_write(v, 1, NULL) : NULL;
  jv_free(v);
  return t;
}

static int is_file_schema(const jv *s)
{
  const char *fmt = jv_str(jv_get(s, "format"));
  if(fmt && (!strcmp(fmt, "binary") || !strcmp(fmt, "base64") || !strcmp(fmt, "byte")))
    return 1;
  if(jv_get(s, "contentMediaType") || jv_get(s, "contentEncoding"))
    return 1;
  return jv_kind(jv_get(s, "items")) == JV_OBJ && is_file_schema(jv_get(s, "items"));
}

static int in_list(const jv *list, const char *name)
{
  int i;
  for(i = 0; i < jv_len(list); i++)
    if(jv_str(jv_at(list, i)) && !strcmp(jv_str(jv_at(list, i)), name))
      return 1;
  return 0;
}

static void rows_of(const jv *schema, const jv *example, const jv *required, oas_fields *rows, int depth)
{
  const jv *props = jv_get(schema, "properties"), *all = jv_get(schema, "allOf");
  int i;
  if(depth > SAMPLE_MAX_DEPTH)
    return;
  for(i = 0; i < jv_len(all); i++)
    rows_of(jv_at(all, i), example, jv_get(jv_at(all, i), "required"), rows, depth + 1);
  for(i = 0; i < jv_len(props); i++) {
    const char *k = jv_key_at(props, i);
    const jv *p = jv_get(props, k);
    const jv *given = jv_kind(example) == JV_OBJ ? jv_get(example, k) : NULL;
    char *text = oas_scalar_text(given ? given : oas_schema_value(p));
    oas_field *f = oas_add_field(rows, k, text, in_list(required, k) || in_list(jv_get(schema, "required"), k));
    if(f)
      f->is_file = is_file_schema(p);
    free(text);
  }
}

void oas_rows_from_schema(const jv *schema, const jv *example, oas_fields *rows)
{
  rows_of(schema, example, NULL, rows, 0);
}

static void describe(const jv *v, char *out, size_t n)
{
  char *t = oas_scalar_text(v);
  snprintf(out, n, "%s", t ? t : "");
  free(t);
}

int oas_read(const jv *root, oas_api *out, char *err, size_t errn)
{
  const jv *oa = jv_get(root, "openapi");
  char version[64];
  memset(out, 0, sizeof(*out));
  if(oa) {
    describe(oa, version, sizeof(version));
    if(version[0] == '3' && (version[1] == '.' || !version[1]))
      return oas_read3(root, out, err, errn);
    snprintf(err, errn, "OpenAPI version %s is not one this app knows", version);
    return -1;
  }
  if(jv_get(root, "swagger")) {
    describe(jv_get(root, "swagger"), version, sizeof(version));
    if(version[0] == '2' && (version[1] == '.' || !version[1]))
      return oas_read2(root, out, err, errn);
    snprintf(err, errn, "Swagger version %s is not one this app knows", version);
    return -1;
  }
  if(jv_get(root, "swaggerVersion"))
    return oas_read1(root, out, err, errn);
  snprintf(err, errn, "This is not an API description: it has no openapi or swagger field");
  return -1;
}
