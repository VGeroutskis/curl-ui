/* The curl option table (generated) and lookups over it. SDL-free. */
#ifndef CURLUI_OPTIONS_H
#define CURLUI_OPTIONS_H

typedef enum {
  OPT_KIND_SINGLE,
  OPT_KIND_BOOLEAN,
  OPT_KIND_APPEND,
  OPT_KIND_MUTEX,
  OPT_KIND_CUSTOM,
  OPT_KIND_PER_URL
} opt_kind;

typedef struct {
  const char *name;        /* long name without dashes: "user-agent" */
  const char *short_flag;  /* "A", or "" */
  const char *arg;         /* "<name>", or "" for a flag */
  const char *help;        /* one line */
  const char *categories;  /* space separated */
  const char *protocols;   /* space separated, may be "" */
  const char *added;       /* version */
  opt_kind kind;
  const char *neg;         /* name of the negated form ("no-foo"), or "" */
  int repeatable;
  int deprecated;
  const char *const *mutexes;   /* NULL terminated or NULL */
  const char *const *see_also;  /* NULL terminated or NULL */
  const char *const *examples;  /* NULL terminated or NULL */
  const char *placeholder;      /* "e.g. svc.txt", from curl's examples */
  const char *description;      /* curl's docs markdown */
} opt_def;

extern const opt_def g_options[];  /* sorted by name */
extern const int g_option_count;

/* Index of an option by long name (leading dashes allowed), or -1. */
int options_find(const char *name);
int options_find_n(const char *name, int len);
int options_in_category(const opt_def *o, const char *category);

/* Library filter names. "All" hides deprecated options unless searching. */
#define OPT_FILTER_ALL "All"
#define OPT_FILTER_POPULAR "Popular"

/* Fills out[] (room for g_option_count) with matching option indexes: name
   matches first, then description matches. Case-insensitive. Returns count. */
int options_search(const char *query, const char *filter, int *out);

#endif
