/* A request as JSON, the same in collections, history and open tabs. It holds {{name}} and
   never the values of variables. */
#ifndef CURLUI_REQJSON_H
#define CURLUI_REQJSON_H

#include <stddef.h>

#include "builder.h"
#include "forms.h"
#include "json.h"

/* extra: options this build does not know, kept as they came. May be NULL. */
jv *req_to_jv(const request *r, const char *method, const form_hints *h, const jv *extra);
/* r and h are freshly initialised. *extra gets a new array of unknown options, or NULL. */
int req_from_jv(const jv *v, request *r, char *method, size_t methodn, form_hints *h, jv **extra);

#endif
