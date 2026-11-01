/* Request model -> argv array and a POSIX shell-quoted preview. No shell is
   ever used to run curl: the preview is only for reading and copying. */
#ifndef CURLUI_BUILDER_H
#define CURLUI_BUILDER_H

#include <stddef.h>

typedef struct {
  int opt;          /* index into g_options */
  int negated;      /* emit the --no-x form */
  char **values;    /* each a buffer the UI edits in place, grown as needed */
  size_t *caps;     /* the size of each */
  int nvalues;
} sel_option;

typedef struct {
  char method[16];  /* "" = let curl decide; HEAD maps to --head */
  char *url;        /* never NULL after request_init */
  size_t url_cap;
  int no_curlrc;            /* --disable, which curl only honours first */
  const char *config_file;  /* --config on every run, or NULL */
  sel_option *sel;
  int nsel;
  int capsel;
} request;

void request_init(request *r);
void request_free(request *r);
void request_clear_options(request *r);
int request_find(const request *r, int opt);  /* slot in sel, or -1 */
sel_option *request_add(request *r, int opt); /* existing slot if present */
void request_remove(request *r, int opt);
char *request_add_value(sel_option *s);       /* a new empty value buffer */
void request_remove_value(sel_option *s, int index);
/* Copies text in, growing the buffer. Returns it, or NULL when out of memory. */
char *request_set_value(sel_option *s, int index, const char *text);
char *request_append_value(sel_option *s, const char *text);
int request_set_url(request *r, const char *url);

typedef enum {
  TOK_PROGRAM,
  TOK_FLAG,
  TOK_VALUE,
  TOK_URL
} tok_kind;

typedef struct {
  char *text;    /* the raw argument */
  tok_kind kind;
  int opt;       /* option behind a flag or value, or -1 */
} arg_token;

typedef struct {
  arg_token *tok;
  int count;
} arg_list;

/* Options with an empty value are left out: the field is unfinished.
   Returns 0, or -1 when out of memory. */
int builder_build(const request *r, const char *program, arg_list *out);
void arg_list_free(arg_list *l);
/* NULL-terminated argv pointing into l; free() the array only. */
const char **arg_list_argv(const arg_list *l, const char *const *extra);

/* 1 when curl would stop with "no URL specified": no URL in the bar, and nothing
   else that supplies one or runs without one (--url, --config, --help, ...). */
int request_lacks_url(const request *r);
/* The value starts like a URL: scheme://something, or www.something */
int value_looks_like_url(const char *value);

/* POSIX shell quoting. Caller frees. */
char *shell_quote(const char *arg);
char *builder_preview(const arg_list *l);

#endif
