/* Hides secrets in what a run printed. It hides exact text and the encodings below, not
   everything: a server can always reflect a secret in a form nothing here recognises. */
#ifndef CURLUI_MASK_H
#define CURLUI_MASK_H

#include <stddef.h>

#include "builder.h"
#include "vars.h"

#define MASK_MIN_LEN 4

typedef struct mask_set mask_set;

/* The secrets of e, as they are and percent encoded, and base64(user:password) of a --user
   or --proxy-user in real that holds one. Longest first. Shorter than MASK_MIN_LEN is left out. */
mask_set *mask_build(const var_env *e, const arg_list *real);
void mask_free(mask_set *m);
/* In place, each match becomes as many * as it has bytes: the length never changes, so
   anything indexing the text stays right. m may be NULL. */
void mask_apply(const mask_set *m, char *buf, size_t len);

char *base64_encode(const char *s, size_t len);  /* caller frees */

#endif
