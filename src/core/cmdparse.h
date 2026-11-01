/* A pasted curl command into a request. Nothing is ever run, and no shell is involved:
   what only a shell could give a meaning to is refused rather than guessed at. */
#ifndef CURLUI_CMDPARSE_H
#define CURLUI_CMDPARSE_H

#include <stddef.h>

#include "builder.h"

/* r is freshly initialised. 0 on success. -1 leaves r empty, with err naming the problem.
   method is a method of the menu given with -X, else "". */
int cmd_parse(const char *text, request *r, char *method, size_t methodn, char *err, size_t errn);

#endif
