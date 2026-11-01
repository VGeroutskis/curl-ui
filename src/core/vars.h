/* Environments of variables, used as {{name}}. The table in the app is the only source of
   values: nothing here reads or writes the process environment. */
#ifndef CURLUI_VARS_H
#define CURLUI_VARS_H

#include <stddef.h>

#include "builder.h"

#define VAR_NAME_CAP 128

typedef struct {
  char *name, *value;
  size_t name_cap, value_cap;  /* the UI edits them in place */
  int secret;
  int reveal;                  /* shown for now; never saved */
} var_t;

typedef struct {
  char *name;
  size_t name_cap;
  var_t *v;
  int n, cap;
} var_env;

typedef struct {
  var_env *e;
  int n, active;
} var_store;

typedef struct {
  char **s;
  int n;
} str_list;

void vars_init(var_store *s);  /* one environment named Default */
void vars_free(var_store *s);
/* A missing file is fine: 0. A damaged one is moved aside and the store starts over: -1,
   with err saying where it went. */
int vars_load(var_store *s, const char *path, char *err, size_t n);
int vars_save(const var_store *s, const char *path);  /* rows with a bad or repeated name stay out */

int vars_name_ok(const char *name);  /* [A-Za-z0-9_.-]+ */
var_t *var_find(const var_env *e, const char *name);
var_t *var_set(var_env *e, const char *name, const char *value, int secret);
void var_remove(var_env *e, int i);
int env_add(var_store *s, const char *name);    /* its index, or -1 for an empty or taken name */
int env_copy(var_store *s, int i, const char *name);
int env_rename(var_store *s, int i, const char *name);
int env_remove(var_store *s, int i);            /* refuses the last one */
int vars_suggest_secret(const char *name);

/* Names used in args that e does not have: once each, in order of use. */
int vars_missing(const var_env *e, const arg_list *tmpl, str_list *out);
void str_list_free(str_list *l);
/* real has the values, shown has ******** for secrets, token for token. In the query of a
   URL a value is percent encoded. A value is not read again for more names, and a name that
   e does not have stays as {{name}}. */
int vars_apply(const var_env *e, const arg_list *tmpl, arg_list *real, arg_list *shown);

#endif
