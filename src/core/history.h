/* What was run, newest last, in a file with one JSON object per line. It holds {{name}},
   the request as written, and no responses. */
#ifndef CURLUI_HISTORY_H
#define CURLUI_HISTORY_H

#include <stddef.h>

#include "json.h"

#define HISTORY_MAX 500

typedef struct {
  long long when;
  int status;
  double secs;
  long long size;
  char *method, *url;
  jv *req;
} hist_entry;

typedef struct {
  hist_entry *e;
  int n;
  int file_lines;  /* in the file, to know when to trim it */
} history;

int history_load(history *h, const char *path);  /* broken lines are skipped */
/* Takes what entry holds. Appends to the file, and rewrites it when it has grown well past the limit. */
int history_append(history *h, const char *path, hist_entry *entry);
int history_clear(history *h, const char *path);
void history_free(history *h);

#endif
