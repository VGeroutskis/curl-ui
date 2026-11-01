/* JSON as a tree, for the files the app reads and writes. json_fmt only checks and
   pretty prints responses. Object keys keep their order, so files diff cleanly. */
#ifndef CURLUI_JSON_H
#define CURLUI_JSON_H

#include <stddef.h>

#define JV_MAX_DEPTH 64

typedef enum { JV_NULL, JV_BOOL, JV_NUM, JV_STR, JV_ARR, JV_OBJ } jv_type;
typedef struct jv jv;

/* NULL on bad input, with "line L column C: ..." in err. \u0000 is refused: strings
   here are C strings. */
jv *jv_parse(const char *text, size_t len, char *err, size_t errlen);
void jv_free(jv *v);

/* All of these accept NULL, as jv_get returns for a missing key. */
jv_type jv_kind(const jv *v);
const char *jv_str(const jv *v);  /* NULL if not a string */
double jv_num(const jv *v);
int jv_bool(const jv *v);
int jv_len(const jv *v);
jv *jv_at(const jv *v, int i);
jv *jv_get(const jv *obj, const char *key);
const char *jv_key_at(const jv *obj, int i);

jv *jv_new(jv_type t);
jv *jv_new_str(const char *s);
jv *jv_new_num(double n);
jv *jv_new_bool(int b);
void jv_set(jv *obj, const char *key, jv *val);  /* takes val; replaces the same key */
void jv_push(jv *arr, jv *val);                  /* takes val */
jv *jv_clone(const jv *v);

char *jv_write(const jv *v, int pretty, size_t *len);  /* caller frees */

#endif
