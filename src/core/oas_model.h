/* What every version of an API description comes down to. */
#ifndef CURLUI_OAS_MODEL_H
#define CURLUI_OAS_MODEL_H

#include <stddef.h>

#include "json.h"

typedef enum { OAS_BODY_NONE, OAS_BODY_JSON, OAS_BODY_URLENC, OAS_BODY_FORM, OAS_BODY_RAW, OAS_BODY_FILE } oas_body_kind;
typedef enum {
  OAS_AUTH_NONE, OAS_AUTH_BASIC, OAS_AUTH_BEARER, OAS_AUTH_DIGEST, OAS_AUTH_KEY_HEADER,
  OAS_AUTH_KEY_QUERY, OAS_AUTH_KEY_COOKIE, OAS_AUTH_OAUTH, OAS_AUTH_MTLS
} oas_auth_kind;

typedef struct {
  char *name, *value;  /* value NULL: use {{name}} */
  int required, is_file;
} oas_field;

typedef struct {
  oas_field *f;
  int n;
} oas_fields;

typedef struct {
  char method[16];
  char *path, *name;
  char **folders;      /* outermost first */
  int nfolders;
  oas_fields query, headers, cookies, body_rows;
  char *raw_query;     /* the name of a parameter that is the whole query */
  oas_body_kind body;
  char *body_text, *content_type;
  oas_auth_kind auth;
  char *auth_name;
} oas_op;

typedef struct {
  char *name, *url;
  oas_fields vars;
} oas_server;

typedef struct {
  char *title;
  oas_server *servers;
  int nservers;
  oas_op *ops;
  int nops;
  int webhooks, callbacks, oauth, alternatives_dropped, xml_bodies;
} oas_api;

void oas_api_free(oas_api *a);
/* A JSON text of what the schema describes. malloc'd. */
char *oas_sample(const jv *schema);
/* Dispatches by version. err names what was found when it is none of them. */
int oas_read(const jv *root, oas_api *out, char *err, size_t errn);
int oas_read3(const jv *root, oas_api *out, char *err, size_t errn);
int oas_read2(const jv *root, oas_api *out, char *err, size_t errn);

/* For the readers. */
char *oas_strdup(const char *s);        /* NULL stays NULL */
oas_op *oas_add_op(oas_api *a, const char *method, const char *path);
oas_server *oas_add_server(oas_api *a, const char *name, const char *url);
oas_field *oas_add_field(oas_fields *l, const char *name, const char *value, int required);
void oas_add_folder(oas_op *op, const char *name);
char *oas_scalar_text(const jv *v);     /* a value as text: NULL for null or nothing */
const jv *oas_schema_value(const jv *schema);  /* example, default, const, first enum, examples[0] */
int oas_is_ignored_header(const char *name);   /* Accept, Content-Type, Authorization */
void oas_rows_from_schema(const jv *schema, const jv *example, oas_fields *rows);
int oas_is_json_media(const char *m);
int oas_is_text_media(const char *m);
int oas_is_xml_media(const char *m);

#endif
