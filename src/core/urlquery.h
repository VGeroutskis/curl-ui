/* The query of a URL: percent encoding, and rows of key and value. */
#ifndef CURLUI_URLQUERY_H
#define CURLUI_URLQUERY_H

#include <stddef.h>

char *pct_encode(const char *s);  /* keeps A-Za-z0-9-_.~; caller frees */
char *pct_decode(const char *s, int plus_is_space);  /* a bad % stays as it is; caller frees */

/* Rows of key and value, which the UI edits in place. */
typedef struct {
  char *key, *value;
  size_t key_cap, value_cap;
  int enabled;
} kv_row;

typedef struct {
  kv_row *r;
  int n, cap;
} kv_list;

kv_row *kv_add(kv_list *l, const char *key, const char *value, int enabled);
void kv_remove(kv_list *l, int i);
void kv_free(kv_list *l);

/* Rows of the query, decoded. {{name}} stays. */
int query_parse(const char *url, kv_list *rows);
/* url with its query replaced by the enabled rows, encoded, and its #fragment kept last.
   {{name}} stays as it is, to be replaced at run time. Rows with no key and no value are out. */
char *query_build(const char *url, const kv_list *rows);

#endif
