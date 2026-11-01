#include "fsdir.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#define SEP '\\'
#else
#include <dirent.h>
#include <limits.h>
#include <unistd.h>
#define SEP '/'
#endif

static char *dup_n(const char *s, size_t n)
{
  char *out = malloc(n + 1);
  if(out) {
    memcpy(out, s, n);
    out[n] = '\0';
  }
  return out;
}

static char *dup_s(const char *s)
{
  return dup_n(s, strlen(s));
}

static int is_sep(char c)
{
#ifdef _WIN32
  return c == '\\' || c == '/';
#else
  return c == '/';
#endif
}

static int ci_cmp(const char *a, const char *b)
{
  for(;; a++, b++) {
    int x = tolower((unsigned char)*a), y = tolower((unsigned char)*b);
    if(x != y)
      return x - y;
    if(!*a)
      return 0;
  }
}

static int entry_cmp(const void *pa, const void *pb)
{
  const fs_entry *a = pa, *b = pb;
  int c;
  if(a->is_dir != b->is_dir)
    return b->is_dir - a->is_dir;
  c = ci_cmp(a->name, b->name);
  return c ? c : strcmp(a->name, b->name);
}

static fs_entry *entry_add(fs_listing *l, int *cap)
{
  if(l->n == *cap) {
    int want = *cap ? *cap * 2 : 64;
    fs_entry *grown = realloc(l->e, (size_t)want * sizeof(*grown));
    if(!grown)
      return NULL;
    l->e = grown;
    *cap = want;
  }
  memset(&l->e[l->n], 0, sizeof(l->e[0]));
  return &l->e[l->n++];
}

void fs_listing_free(fs_listing *l)
{
  int i;
  for(i = 0; i < l->n; i++)
    free(l->e[i].name);
  free(l->e);
  l->e = NULL;
  l->n = 0;
}

#ifdef _WIN32

static wchar_t *to_wide(const char *s)
{
  int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
  wchar_t *w = n > 0 ? malloc((size_t)n * sizeof(wchar_t)) : NULL;
  if(w)
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
  return w;
}

static char *to_utf8(const wchar_t *w)
{
  int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
  char *s = n > 0 ? malloc((size_t)n) : NULL;
  if(s)
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, NULL, NULL);
  return s;
}

int fs_list(const char *dir, fs_listing *out, char *err, size_t errn)
{
  char *pattern = fs_join(dir, "*");
  wchar_t *wp = pattern ? to_wide(pattern) : NULL;
  WIN32_FIND_DATAW fd;
  HANDLE h;
  int cap = 0;
  memset(out, 0, sizeof(*out));
  h = wp ? FindFirstFileW(wp, &fd) : INVALID_HANDLE_VALUE;
  free(pattern);
  free(wp);
  if(h == INVALID_HANDLE_VALUE) {
    snprintf(err, errn, "Cannot open this folder (error %lu)", (unsigned long)GetLastError());
    return -1;
  }
  do {
    fs_entry *e;
    if(!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L".."))
      continue;
    e = entry_add(out, &cap);
    if(!e)
      break;
    e->name = to_utf8(fd.cFileName);
    if(!e->name) {
      out->n--;
      continue;
    }
    e->is_dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    e->hidden = (fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0;
    e->size = e->is_dir ? -1 : ((long long)fd.nFileSizeHigh << 32) + fd.nFileSizeLow;
    {
      unsigned long long t = ((unsigned long long)fd.ftLastWriteTime.dwHighDateTime << 32) + fd.ftLastWriteTime.dwLowDateTime;
      e->mtime = t > 116444736000000000ULL ? (long long)((t - 116444736000000000ULL) / 10000000ULL) : 0;
    }
  } while(FindNextFileW(h, &fd));
  FindClose(h);
  if(out->n)
    qsort(out->e, (size_t)out->n, sizeof(out->e[0]), entry_cmp);
  return 0;
}

int fs_kind(const char *path)
{
  wchar_t *w = to_wide(path);
  DWORD at = w ? GetFileAttributesW(w) : INVALID_FILE_ATTRIBUTES;
  free(w);
  if(at == INVALID_FILE_ATTRIBUTES)
    return FS_MISSING;
  return (at & FILE_ATTRIBUTE_DIRECTORY) ? FS_DIR : FS_FILE;
}

char *fs_canon(const char *path)
{
  wchar_t *w = to_wide(path);
  wchar_t *full;
  DWORD n;
  char *out = NULL;
  if(!w)
    return NULL;
  n = GetFullPathNameW(w, 0, NULL, NULL);
  full = n ? malloc((size_t)n * sizeof(wchar_t)) : NULL;
  if(full && GetFullPathNameW(w, n, full, NULL))
    out = to_utf8(full);
  free(full);
  free(w);
  if(out) {  /* no trailing separator, except on a drive */
    size_t len = strlen(out);
    while(len > 3 && is_sep(out[len - 1]))
      out[--len] = '\0';
  }
  return out;
}

int fs_is_absolute(const char *p)
{
  if(is_sep(p[0]) && is_sep(p[1]))
    return 1;
  return isalpha((unsigned char)p[0]) && p[1] == ':' && is_sep(p[2]);
}

const char *fs_home(void)
{
  const char *h = getenv("USERPROFILE");
  return h && *h ? h : NULL;
}

unsigned fs_drives(void)
{
  return (unsigned)GetLogicalDrives();
}

/* C:\ and \\server\share\ have nothing above them */
static size_t root_len(const char *p)
{
  if(isalpha((unsigned char)p[0]) && p[1] == ':')
    return is_sep(p[2]) ? 3 : 2;
  if(is_sep(p[0]) && is_sep(p[1])) {
    const char *c = p + 2;
    int parts = 0;
    while(*c && parts < 2) {
      if(is_sep(*c))
        parts++;
      c++;
    }
    return (size_t)(c - p);
  }
  return is_sep(p[0]) ? 1 : 0;
}

#else /* POSIX */

int fs_list(const char *dir, fs_listing *out, char *err, size_t errn)
{
  DIR *d = opendir(dir);
  struct dirent *de;
  int cap = 0;
  memset(out, 0, sizeof(*out));
  if(!d) {
    snprintf(err, errn, "Cannot open this folder: %s", strerror(errno));
    return -1;
  }
  while((de = readdir(d)) != NULL) {
    struct stat st;
    fs_entry *e;
    char *full;
    if(!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
      continue;
    e = entry_add(out, &cap);
    if(!e)
      break;
    e->name = dup_s(de->d_name);
    full = fs_join(dir, de->d_name);
    if(!e->name || !full) {
      free(e->name);
      free(full);
      out->n--;
      continue;
    }
    e->hidden = de->d_name[0] == '.';
    e->size = -1;
    /* a broken link is still there, as a file of unknown size */
    if(!stat(full, &st)) {
      e->is_dir = S_ISDIR(st.st_mode);
      e->size = e->is_dir ? -1 : (long long)st.st_size;
      e->mtime = (long long)st.st_mtime;
    }
    free(full);
  }
  closedir(d);
  if(out->n)
    qsort(out->e, (size_t)out->n, sizeof(out->e[0]), entry_cmp);
  return 0;
}

int fs_kind(const char *path)
{
  struct stat st;
  if(stat(path, &st))
    return FS_MISSING;
  return S_ISDIR(st.st_mode) ? FS_DIR : FS_FILE;
}

/* Folds . and .. by hand: what comes after a missing folder cannot be asked of the system. */
char *fs_canon(const char *path)
{
  char *abs_path = NULL, *resolved, *out;
  size_t n;
  const char *c;
  size_t w = 0;
  if(path[0] == '/')
    abs_path = dup_s(path);
  else {
    char cwd[4096];
    if(!getcwd(cwd, sizeof(cwd)))
      return NULL;
    abs_path = fs_join(cwd, path);
  }
  if(!abs_path)
    return NULL;
  resolved = realpath(abs_path, NULL);
  if(resolved) {
    free(abs_path);
    return resolved;
  }
  n = strlen(abs_path);
  out = malloc(n + 2);
  if(!out) {
    free(abs_path);
    return NULL;
  }
  c = abs_path;
  while(*c) {
    const char *end;
    size_t len;
    while(*c == '/')
      c++;
    end = c;
    while(*end && *end != '/')
      end++;
    len = (size_t)(end - c);
    if(len == 2 && c[0] == '.' && c[1] == '.') {
      while(w && out[w - 1] != '/')
        w--;
      if(w)
        w--;
    }
    else if(len && !(len == 1 && c[0] == '.')) {
      out[w++] = '/';
      memcpy(out + w, c, len);
      w += len;
    }
    c = end;
  }
  if(!w)
    out[w++] = '/';
  out[w] = '\0';
  free(abs_path);
  return out;
}

int fs_is_absolute(const char *p)
{
  return p[0] == '/';
}

const char *fs_home(void)
{
  const char *h = getenv("HOME");
  return h && *h ? h : NULL;
}

unsigned fs_drives(void)
{
  return 0;
}

static size_t root_len(const char *p)
{
  return p[0] == '/' ? 1 : 0;
}

#endif

char *fs_join(const char *dir, const char *name)
{
  size_t d = strlen(dir), n = strlen(name);
  int need = d && !is_sep(dir[d - 1]);
  char *out = malloc(d + n + 2);
  if(out) {
    memcpy(out, dir, d);
    if(need)
      out[d] = SEP;
    memcpy(out + d + (size_t)need, name, n + 1);
  }
  return out;
}

char *fs_resolve(const char *dir, const char *name)
{
  if(fs_is_absolute(name))
    return dup_s(name);
  if(name[0] == '~' && (!name[1] || is_sep(name[1])) && fs_home())
    return name[1] && name[2] ? fs_join(fs_home(), name + 2) : dup_s(fs_home());
  return fs_join(dir, name);
}

char *fs_parent(const char *path)
{
  size_t root = root_len(path), n = strlen(path);
  while(n > root && is_sep(path[n - 1]))
    n--;
  if(n <= root)
    return NULL;
  while(n > root && !is_sep(path[n - 1]))
    n--;
  while(n > root && is_sep(path[n - 1]))
    n--;
  return n ? dup_n(path, n) : NULL;
}

const char *fs_basename(const char *path)
{
  const char *c, *base = path;
  for(c = path; *c; c++)
    if(is_sep(*c) && c[1])
      base = c + 1;
  return base;
}

int fs_name_matches(const char *name, const char *filter)
{
  size_t f = strlen(filter);
  const char *c;
  if(!f)
    return 1;
  for(c = name; *c; c++) {
    size_t i = 0;
    while(i < f && c[i] && tolower((unsigned char)c[i]) == tolower((unsigned char)filter[i]))
      i++;
    if(i == f)
      return 1;
  }
  return 0;
}

/* ---- options ---- */

typedef struct {
  const char *name;
  fs_pick_mode mode;
  fs_value_form form;
} opt_path;

/* Read from curl's docs one by one. A file curl both reads and writes (a cookie jar, the
   HSTS cache) is a save: it need not exist yet. */
static const opt_path g_paths[] = {
  { "alt-svc", FS_PICK_SAVE, FS_VALUE_PLAIN },
  { "cacert", FS_PICK_OPEN, FS_VALUE_PLAIN },
  { "capath", FS_PICK_DIR, FS_VALUE_PLAIN },
  { "cert", FS_PICK_OPEN, FS_VALUE_CERT },
  { "config", FS_PICK_OPEN, FS_VALUE_PLAIN },
  { "cookie", FS_PICK_OPEN, FS_VALUE_PLAIN },
  { "cookie-jar", FS_PICK_SAVE, FS_VALUE_PLAIN },
  { "crlfile", FS_PICK_OPEN, FS_VALUE_PLAIN },
  { "data", FS_PICK_OPEN, FS_VALUE_AT },
  { "data-ascii", FS_PICK_OPEN, FS_VALUE_AT },
  { "data-binary", FS_PICK_OPEN, FS_VALUE_AT },
  { "data-urlencode", FS_PICK_OPEN, FS_VALUE_NAME_AT },
  { "dump-header", FS_PICK_SAVE, FS_VALUE_PLAIN },
  { "egd-file", FS_PICK_OPEN, FS_VALUE_PLAIN },
  { "etag-compare", FS_PICK_OPEN, FS_VALUE_PLAIN },
  { "etag-save", FS_PICK_SAVE, FS_VALUE_PLAIN },
  { "form", FS_PICK_OPEN, FS_VALUE_FORM },
  { "header", FS_PICK_OPEN, FS_VALUE_AT },
  { "hsts", FS_PICK_SAVE, FS_VALUE_PLAIN },
  { "httpsig-key", FS_PICK_OPEN, FS_VALUE_PLAIN },
  { "json", FS_PICK_OPEN, FS_VALUE_AT },
  { "key", FS_PICK_OPEN, FS_VALUE_PLAIN },
  { "knownhosts", FS_PICK_OPEN, FS_VALUE_PLAIN },
  { "libcurl", FS_PICK_SAVE, FS_VALUE_PLAIN },
  { "netrc-file", FS_PICK_OPEN, FS_VALUE_PLAIN },
  { "output", FS_PICK_SAVE, FS_VALUE_PLAIN },
  { "output-dir", FS_PICK_DIR, FS_VALUE_PLAIN },
  { "pinnedpubkey", FS_PICK_OPEN, FS_VALUE_PLAIN },
  { "proxy-cacert", FS_PICK_OPEN, FS_VALUE_PLAIN },
  { "proxy-capath", FS_PICK_DIR, FS_VALUE_PLAIN },
  { "proxy-cert", FS_PICK_OPEN, FS_VALUE_CERT },
  { "proxy-crlfile", FS_PICK_OPEN, FS_VALUE_PLAIN },
  { "proxy-header", FS_PICK_OPEN, FS_VALUE_AT },
  { "proxy-key", FS_PICK_OPEN, FS_VALUE_PLAIN },
  { "proxy-pinnedpubkey", FS_PICK_OPEN, FS_VALUE_PLAIN },
  { "pubkey", FS_PICK_OPEN, FS_VALUE_PLAIN },
  { "random-file", FS_PICK_OPEN, FS_VALUE_PLAIN },
  { "ssl-sessions", FS_PICK_SAVE, FS_VALUE_PLAIN },
  { "stderr", FS_PICK_SAVE, FS_VALUE_PLAIN },
  { "trace", FS_PICK_SAVE, FS_VALUE_PLAIN },
  { "trace-ascii", FS_PICK_SAVE, FS_VALUE_PLAIN },
  { "unix-socket", FS_PICK_OPEN, FS_VALUE_PLAIN },
  { "upload-file", FS_PICK_OPEN, FS_VALUE_PLAIN },
  { "url-query", FS_PICK_OPEN, FS_VALUE_NAME_AT },
  { "variable", FS_PICK_OPEN, FS_VALUE_NAME_AT },
};

fs_pick_mode fs_option_mode(const char *option, fs_value_form *form)
{
  size_t i;
  *form = FS_VALUE_PLAIN;
  for(i = 0; i < sizeof(g_paths) / sizeof(g_paths[0]); i++) {
    if(!strcmp(g_paths[i].name, option)) {
      *form = g_paths[i].form;
      return g_paths[i].mode;
    }
  }
  return FS_PICK_NONE;
}

/* Where path[:password] ends its path: curl takes \: and \\ as escapes, and on Windows the
   colon of a drive as part of the path. */
static const char *cert_password(const char *v)
{
  const char *c = v;
  if(isalpha((unsigned char)v[0]) && v[1] == ':' && (v[2] == '\\' || v[2] == '/'))
    c = v + 2;
  for(; *c; c++) {
    if(*c == '\\' && c[1])
      c++;
    else if(*c == ':')
      return c;
  }
  return NULL;
}

static char *cert_value(const char *old, const char *path)
{
  const char *pw = cert_password(old);
  size_t n = strlen(path), w = 0, i = 0;
  char *out = malloc(n * 2 + (pw ? strlen(pw) : 0) + 1);
  if(!out)
    return NULL;
  if(isalpha((unsigned char)path[0]) && path[1] == ':' && (path[2] == '\\' || path[2] == '/')) {
    out[w++] = path[0];
    out[w++] = ':';
    i = 2;
  }
  for(; i < n; i++) {
    if(path[i] == ':' || path[i] == '\\')
      out[w++] = '\\';
    out[w++] = path[i];
  }
  out[w] = '\0';
  if(pw)
    strcpy(out + w, pw);
  return out;
}

char *fs_option_value(const char *option, const char *old, const char *path)
{
  fs_value_form form;
  size_t n = strlen(path);
  char *out;
  fs_option_mode(option, &form);
  switch(form) {
  case FS_VALUE_PLAIN:
    return dup_s(path);
  case FS_VALUE_AT:
    out = malloc(n + 2);
    if(out)
      sprintf(out, "@%s", path);
    return out;
  case FS_VALUE_CERT:
    return cert_value(old, path);
  case FS_VALUE_NAME_AT: {
    size_t name = strcspn(old, "@=");
    if(!old[name])
      name = 0;
    out = malloc(name + n + 2);
    if(out) {
      memcpy(out, old, name);
      out[name] = '@';
      memcpy(out + name + 1, path, n + 1);
    }
    return out;
  }
  case FS_VALUE_FORM: {
    const char *eq = strchr(old, '=');
    const char *name = eq && eq != old ? old : "file";
    size_t nl = eq && eq != old ? (size_t)(eq - old) : 4;
    int quote = strpbrk(path, ";,\"") != NULL;
    size_t w, i;
    out = malloc(nl + n * 2 + 6);
    if(!out)
      return NULL;
    memcpy(out, name, nl);
    w = nl;
    out[w++] = '=';
    out[w++] = '@';
    if(quote)
      out[w++] = '"';
    for(i = 0; i < n; i++) {
      if(quote && (path[i] == '"' || path[i] == '\\'))
        out[w++] = '\\';
      out[w++] = path[i];
    }
    if(quote)
      out[w++] = '"';
    out[w] = '\0';
    return out;
  }
  }
  return NULL;
}

char *fs_option_path(const char *option, const char *value)
{
  fs_value_form form;
  fs_pick_mode mode = fs_option_mode(option, &form);
  if(mode == FS_PICK_NONE)
    return dup_s("");
  switch(form) {
  case FS_VALUE_PLAIN:
    return dup_s(value);
  case FS_VALUE_AT:
    return dup_s(value[0] == '@' ? value + 1 : "");
  case FS_VALUE_NAME_AT: {
    const char *at = strchr(value, '@');
    return dup_s(at ? at + 1 : "");
  }
  case FS_VALUE_CERT: {
    const char *pw = cert_password(value);
    size_t n = pw ? (size_t)(pw - value) : strlen(value), i, w = 0;
    char *out = malloc(n + 1);
    if(!out)
      return NULL;
    for(i = 0; i < n; i++) {
      if(value[i] == '\\' && i + 1 < n && (value[i + 1] == ':' || value[i + 1] == '\\'))
        i++;
      out[w++] = value[i];
    }
    out[w] = '\0';
    return out;
  }
  case FS_VALUE_FORM: {
    const char *at = strstr(value, "=@");
    const char *c;
    char *out;
    size_t w = 0;
    if(!at)
      return dup_s("");
    c = at + 2;
    out = malloc(strlen(c) + 1);
    if(!out)
      return NULL;
    if(*c == '"') {
      for(c++; *c && *c != '"'; c++) {
        if(*c == '\\' && c[1])
          c++;
        out[w++] = *c;
      }
    }
    else {
      for(; *c && *c != ';' && *c != ','; c++)
        out[w++] = *c;
    }
    out[w] = '\0';
    return out;
  }
  }
  return dup_s("");
}

/* ---- our own title bar ---- */

win_hit win_hit_test(float x, float y, float w, float h, float drag_h, float drag_right, int maximized)
{
  if(!maximized) {
    int west = x < WIN_EDGE, east = x >= w - WIN_EDGE;
    int north = y < WIN_EDGE, south = y >= h - WIN_EDGE;
    if(north && west)
      return WIN_HIT_NW;
    if(north && east)
      return WIN_HIT_NE;
    if(south && west)
      return WIN_HIT_SW;
    if(south && east)
      return WIN_HIT_SE;
    if(west)
      return WIN_HIT_W;
    if(east)
      return WIN_HIT_E;
    if(north)
      return x < drag_right ? WIN_HIT_N : WIN_HIT_NONE;
    if(south)
      return WIN_HIT_S;
  }
  return y < drag_h && x < drag_right ? WIN_HIT_DRAG : WIN_HIT_NONE;
}
