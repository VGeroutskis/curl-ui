/* What the forms read and write: only options of the request, plus the little a form knows
   that a command cannot say, such as a row switched off. */
#ifndef CURLUI_FORMS_H
#define CURLUI_FORMS_H

#include <stddef.h>

#include "builder.h"
#include "urlquery.h"

typedef enum { BODY_NONE, BODY_RAW, BODY_JSON, BODY_FORMDATA, BODY_URLENC, BODY_FILE, BODY_OPTIONS } body_kind;
typedef enum { AUTH_NONE, AUTH_BASIC, AUTH_BEARER, AUTH_DIGEST, AUTH_APIKEY, AUTH_OPTIONS } auth_kind;

typedef struct {
  char auth[16];       /* "apikey" when the auth form set one, since a command cannot tell */
  char *apikey_name;
  char apikey_where[8];
  kv_list disabled_params, disabled_headers, disabled_body;
} form_hints;

void form_hints_init(form_hints *h);
void form_hints_free(form_hints *h);

/* Enabled rows from --header, then those switched off from h. */
void form_headers_get(const request *r, const form_hints *h, kv_list *rows);
void form_headers_set(request *r, form_hints *h, const kv_list *rows);

/* BODY_OPTIONS: the body options say more than the form can, and it leaves them alone. */
body_kind form_body_kind(const request *r);
int form_body_get(const request *r, kv_list *rows, char **text);  /* rows or text, by kind */
int form_body_set(request *r, body_kind k, const kv_list *rows, const char *text, char *err, size_t n);

/* Likewise for AUTH_OPTIONS. */
auth_kind form_auth_kind(const request *r, const form_hints *h);
/* a, b: user and password; token and NULL; API key name and value. */
int form_auth_get(const request *r, const form_hints *h, char **a, char **b);
/* where: "header" or "query", for an API key. */
int form_auth_set(request *r, form_hints *h, auth_kind k, const char *a, const char *b, const char *where,
                  char *err, size_t n);
int form_auth_literal_secret(const request *r);  /* a password or token not written as {{...}} */

#endif
