/* The operating system limits how long a command line can be. Past that, arguments go
   to curl through a private --config file instead. */
#ifndef CURLUI_ARGFILE_H
#define CURLUI_ARGFILE_H

#include <stddef.h>

#include "builder.h"

#define ARG_SPILL_ONE 100000u
#define ARG_SPILL_TOTAL 200000u
#define ARG_SPILL_WIN_CMDLINE 30000u
#define ARG_CONFIG_LINE_MAX (10u * 1024u * 1024u)  /* curl's MAX_CONFIG_LINE_LENGTH */

int args_need_file(const char *const *argv);
/* Everything in l but the program and --disable, one option per line. Returns 0, or -1
   with err naming the option too long for curl to read back. */
int args_write_config(const arg_list *l, const char *path, char *err, size_t n);
/* Deletes run-*.conf that a crash left in dir. */
void args_remove_stale(const char *dir);

#endif
