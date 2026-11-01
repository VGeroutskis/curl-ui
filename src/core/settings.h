/* User settings, and the request an older version remembered: small key=value text files that
   can also be edited by hand. Anything malformed falls back to the default. */
#ifndef CURLUI_SETTINGS_H
#define CURLUI_SETTINGS_H

#include <stddef.h>

#include "builder.h"

#define SETTINGS_PATH_CAP 1024

typedef struct {
  int dark;
  int zoom;           /* percent; 0 detects it from the display */
  char program[SETTINGS_PATH_CAP];
  char config_file[SETTINGS_PATH_CAP];  /* passed as --config on every run */
  int no_curlrc;      /* start with --disable: curl skips ~/.curlrc */
  int pretty;         /* Pretty JSON on by default */
  int capture;        /* add --verbose etc. for the Log tab and status chips */
  int hover_ms;       /* docs panel hover delay */
  int reopen_tabs;    /* open tabs come back at the next start */
  int legacy_remember; /* an older version kept one request in last-request.txt */
  int win_w, win_h;   /* 0 means the default size */
} settings_t;

void settings_defaults(settings_t *s);
/* Writes dir/name, where dir is the per-user config directory (or override when
   it is set). Returns 0, or -1 if it cannot be determined. */
int settings_path(const char *override, const char *name, char *out, size_t n);
/* A missing or unreadable file leaves the defaults. Returns 1 if a file was read. */
int settings_load(settings_t *s, const char *path);
/* Creates the directory. The file is private to the user and replaced whole,
   so a crash cannot leave half of it. Returns 0 on success. */
int settings_save(const settings_t *s, const char *path);

/* The request can hold passwords: same private file treatment. */
int request_save(const request *r, int method, const char *path);
int request_load(request *r, int *method, const char *path);

#endif
