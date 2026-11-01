/* The file system as the file picker sees it, and which curl options take a path. SDL-free. */
#ifndef CURLUI_FSDIR_H
#define CURLUI_FSDIR_H

#include <stddef.h>

typedef struct {
  char *name;       /* UTF-8 on Windows; bytes as they are elsewhere */
  int is_dir;       /* a link counts as what it points to */
  int hidden;
  long long size;   /* -1 when unknown */
  long long mtime;  /* seconds since 1970, 0 when unknown */
} fs_entry;

typedef struct {
  fs_entry *e;
  int n;
} fs_listing;

/* Folders first, then files, by name without regard to ASCII case. 0, or -1 with err. */
int fs_list(const char *dir, fs_listing *out, char *err, size_t errn);
void fs_listing_free(fs_listing *l);

enum { FS_MISSING, FS_FILE, FS_DIR };
int fs_kind(const char *path);
/* An absolute path with no . or .. in it. A path that does not exist is still made absolute.
   Returns a malloc'd string, or NULL. */
char *fs_canon(const char *path);
char *fs_join(const char *dir, const char *name);        /* malloc'd */
/* What a name typed in the picker means inside dir: an absolute path or ~/x stands alone. */
char *fs_resolve(const char *dir, const char *name);     /* malloc'd */
int fs_is_absolute(const char *path);
/* The folder holding path, malloc'd. NULL at the top. */
char *fs_parent(const char *path);
const char *fs_basename(const char *path);
const char *fs_home(void);                               /* may be NULL */
unsigned fs_drives(void);                                /* Windows: bit 0 is A:. 0 elsewhere */
int fs_name_matches(const char *name, const char *filter); /* ASCII case-insensitive part of name */

/* What a curl option's value wants from the picker. */
typedef enum { FS_PICK_NONE, FS_PICK_OPEN, FS_PICK_SAVE, FS_PICK_DIR } fs_pick_mode;
/* And how the chosen path enters the value. */
typedef enum {
  FS_VALUE_PLAIN,   /* the path */
  FS_VALUE_AT,      /* @path */
  FS_VALUE_NAME_AT, /* name@path: --data-urlencode, --variable */
  FS_VALUE_FORM,    /* name=@path: --form */
  FS_VALUE_CERT     /* path[:password]: the password stays */
} fs_value_form;

fs_pick_mode fs_option_mode(const char *option, fs_value_form *form);
/* The option's new value given the chosen path. malloc'd. */
char *fs_option_value(const char *option, const char *old, const char *path);
/* The path an option's value names, for where the picker should start. malloc'd, may be "". */
char *fs_option_path(const char *option, const char *value);

/* Which part of the window a point is, with our own title bar. */
typedef enum {
  WIN_HIT_NONE, WIN_HIT_DRAG, WIN_HIT_N, WIN_HIT_S, WIN_HIT_E, WIN_HIT_W, WIN_HIT_NE, WIN_HIT_NW, WIN_HIT_SE, WIN_HIT_SW
} win_hit;
#define WIN_EDGE 6.0f
/* Window units. drag_h is the height of the bar, drag_right where its controls begin. */
win_hit win_hit_test(float x, float y, float w, float h, float drag_h, float drag_right, int maximized);

#endif
