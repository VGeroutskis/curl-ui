/* An API description and all it refers to, as one tree with nothing left to look up. */
#ifndef CURLUI_OAS_LOAD_H
#define CURLUI_OAS_LOAD_H

#include <stddef.h>

#include "json.h"
#include "vars.h"

#define OAS_MAX_FILES 256
#define OAS_MAX_BYTES ((size_t)64 << 20)
#define OAS_ITEM_NODES 5000L
#define OAS_TOTAL_NODES 2000000L

typedef struct {
  int files, remote_refs, broken_refs, cycles;
  int truncated;     /* references not followed, under a schema too large to be of use */
  str_list outside;  /* files read from outside the folder of the first */
} oas_load_report;

/* NULL with the reason in err. rep is filled either way; free it with the function below.
   A $ref to a URL is left out and counted, as is one to nothing. A cycle stops with {}. Swagger 1.x
   names its models in $ref, so there they stay; its declarations go under _declarations.
   Of OpenAPI and Swagger 2.0 only what makes requests is inlined: real ones refer to everything from
   everywhere, so responses, callbacks, webhooks and reusable components are left out, and past
   OAS_ITEM_NODES under one path what is left of its references is {}. */
jv *oas_load(const char *path, oas_load_report *rep, char *err, size_t errn);
void oas_load_report_free(oas_load_report *r);

#endif
