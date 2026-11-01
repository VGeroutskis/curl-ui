/* Runs a program with stdin on the null device and stdout/stderr delivered
   to callbacks from reader threads. Never goes through a shell. */
#ifndef CURLUI_PROCESS_H
#define CURLUI_PROCESS_H

#include <stddef.h>

typedef void (*proc_sink)(void *user, const char *data, size_t len);
typedef struct proc proc_t;

#define PROC_EXIT_SIGNAL_BASE 1000 /* exit code = base + signal on POSIX */

/* NULL on failure, with a message in err. argv[0] is looked up on PATH. */
proc_t *proc_start(const char *const *argv, proc_sink on_out, proc_sink on_err,
                   void *user, char *err, size_t errlen);
int proc_finished(proc_t *p);
void proc_kill(proc_t *p);
/* Blocks until exit, returns the exit code. Safe to call repeatedly. */
int proc_wait(proc_t *p);
void proc_free(proc_t *p); /* kills if still running */

/* Windows command line for argv, using the rules CommandLineToArgvW parses.
   Compiled everywhere so it can be tested everywhere. Caller frees. */
char *proc_win_cmdline(const char *const *argv);

#endif
