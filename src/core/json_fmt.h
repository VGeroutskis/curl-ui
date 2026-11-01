/* Strict JSON validator and pretty printer. */
#ifndef CURLUI_JSON_FMT_H
#define CURLUI_JSON_FMT_H

#include <stddef.h>

#define JSON_MAX_DEPTH 128

/* Returns a NUL-terminated pretty form (caller frees), or NULL when the input
   is not one valid JSON value or nests deeper than JSON_MAX_DEPTH. 2-space
   indent; short arrays of scalars stay on one line. */
char *json_pretty(const char *text, size_t len, size_t *out_len);

#endif
