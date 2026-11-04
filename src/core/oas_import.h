/* An API description becomes a collection and the variables of that collection. Nothing here
   touches the app: the caller adds what comes back. */
#ifndef CURLUI_OAS_IMPORT_H
#define CURLUI_OAS_IMPORT_H

#include <stddef.h>

#include "collection.h"
#include "oas_load.h"
#include "vars.h"

typedef struct {
  int requests;
  oas_load_report load;
  int webhooks, callbacks, oauth, alternatives_dropped, xml_bodies;
  int unsafe;             /* parts left out, that would have made curl read a file or take an option */
  char *relative_server;  /* the first with no host */
} oas_report;

/* taken_names: of the collections there are, for "Name 2". vars is made here. rep needs
   oas_report_free either way. Returns 0, or -1 with the reason and nothing made. */
int oas_import_file(const char *path, const char *const *taken_names, int ntaken, collection **out, var_store *vars,
                    oas_report *rep, char *err, size_t errn);
void oas_report_free(oas_report *r);
void oas_report_text(const oas_report *r, const char *coll_name, char *out, size_t n);
/* Is there something in it the user should act on? What is skipped by design is not. */
int oas_report_warns(const oas_report *r);
/* Does this text say it is one? It may also be a curl command. */
int oas_looks_like_api(const char *text, size_t len);

#endif
