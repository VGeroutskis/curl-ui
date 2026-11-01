/* The operating system limits how long a command line can be. Past that, arguments go
   to curl through a private --config file instead. */
#ifndef CURLUI_ARGFILE_H
#define CURLUI_ARGFILE_H

#include <stddef.h>

#include "builder.h"

#define ARG_SPILL_ONE 100000u
#define ARG_SPILL_TOTAL 200000u
#define ARG_SPILL_WIN_CMDLINE 30000u
#define ARG_CONFIG_LINE_MAX (10u * 1024u * 1024u)  /* curl's MAX_CONFIG_LINE_LENGTH since 8.2 */
#define ARG_CONFIG_LINE_OLD (100u * 1024u)         /* and before it */
#define ARG_BODY_SPILL 50000u

int args_need_file(const char *const *argv);
typedef struct {
  char **paths;
  int n;
} arg_files;

/* A long body goes into a file of its own, which curl reads with no limit on its size:
   --data, --data-ascii, --data-raw and --data-binary become --data-binary @file, which
   sends the same bytes, and --json becomes --json @file. A value that already names a file
   stays, except for --data-raw, where @ means nothing. Files are stem-bN.conf. */
int args_spill_bodies(arg_list *l, const char *stem, arg_files *out, char *err, size_t n);
void args_files_remove(arg_files *f);  /* deletes them and empties f */

/* Everything in l but the program and --disable, one option per line. Returns 0, or -1
   with err naming the option too long for curl to read back. */
int args_write_config(const arg_list *l, const char *path, size_t line_max, char *err, size_t n);
/* Deletes run-*.conf that a crash left in dir. */
void args_remove_stale(const char *dir);

#endif
