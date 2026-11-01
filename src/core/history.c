#include "history.h"
#include "fileio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HISTORY_REWRITE_AT (HISTORY_MAX + 100)
#define HISTORY_FILE_MAX ((size_t)1 << 31)

static void entry_free(hist_entry *e)
{
  free(e->method);
  free(e->url);
  jv_free(e->req);
  memset(e, 0, sizeof(*e));
}

static char *entry_line(const hist_entry *e, size_t *n)
{
  jv *o = jv_new(JV_OBJ);
  char *s;
  jv_set(o, "when", jv_new_num((double)e->when));
  jv_set(o, "status", jv_new_num(e->status));
  jv_set(o, "secs", jv_new_num(e->secs));
  jv_set(o, "size", jv_new_num((double)e->size));
  jv_set(o, "method", jv_new_str(e->method));
  jv_set(o, "url", jv_new_str(e->url));
  jv_set(o, "request", jv_clone(e->req));
  s = jv_write(o, 0, n);
  jv_free(o);
  return s;
}

static char *dup_or_empty(const char *s)
{
  size_t n = strlen(s ? s : "") + 1;
  char *d = malloc(n);
  if(d)
    memcpy(d, s ? s : "", n);
  return d;
}

static void push(history *h, const hist_entry *e)
{
  if(h->n == HISTORY_MAX) {
    entry_free(&h->e[0]);
    memmove(&h->e[0], &h->e[1], sizeof(hist_entry) * (HISTORY_MAX - 1));
    h->n--;
  }
  if(!h->e)
    h->e = calloc(HISTORY_MAX, sizeof(hist_entry));
  if(h->e)
    h->e[h->n++] = *e;
}

int history_load(history *h, const char *path)
{
  size_t len = 0;
  char *text = file_read_all(path, &len, HISTORY_FILE_MAX);
  char *p;
  h->n = 0;
  if(!text)
    return 0;
  for(p = text; p < text + len;) {
    char *nl = memchr(p, '\n', (size_t)(text + len - p));
    size_t ln = nl ? (size_t)(nl - p) : (size_t)(text + len - p);
    jv *o = ln ? jv_parse(p, ln, NULL, 0) : NULL;
    if(jv_kind(o) == JV_OBJ && jv_get(o, "request")) {
      hist_entry e;
      memset(&e, 0, sizeof(e));
      e.when = (long long)jv_num(jv_get(o, "when"));
      e.status = (int)jv_num(jv_get(o, "status"));
      e.secs = jv_num(jv_get(o, "secs"));
      e.size = (long long)jv_num(jv_get(o, "size"));
      e.method = dup_or_empty(jv_str(jv_get(o, "method")));
      e.url = dup_or_empty(jv_str(jv_get(o, "url")));
      e.req = jv_clone(jv_get(o, "request"));
      push(h, &e);
    }
    jv_free(o);
    p += ln + 1;
  }
  free(text);
  return 0;
}

static int rewrite(const history *h, const char *path)
{
  size_t tn = strlen(path) + 8;
  char *tmp = malloc(tn);
  FILE *f;
  int i, rc;
  if(!tmp)
    return -1;
  snprintf(tmp, tn, "%s.tmp", path);
  file_make_parents(path);
  f = file_open_private(tmp);
  if(!f) {
    free(tmp);
    return -1;
  }
  for(i = 0; i < h->n; i++) {
    size_t n = 0;
    char *line = entry_line(&h->e[i], &n);
    if(line) {
      fwrite(line, 1, n, f);
      fputc('\n', f);
      free(line);
    }
  }
  rc = file_commit(f, tmp, path);
  free(tmp);
  return rc;
}

int history_append(history *h, const char *path, hist_entry *entry)
{
  size_t n = 0;
  char *line = entry_line(entry, &n);
  FILE *f;
  int rc = -1;
  static int appended;
  push(h, entry);
  memset(entry, 0, sizeof(*entry));
  if(!line)
    return -1;
  /* created private, and appended to from then on */
  f = fopen(path, "rb");
  if(f) {
    fclose(f);
    f = fopen(path, "ab");
  }
  else {
    file_make_parents(path);
    f = file_open_private(path);
  }
  if(f) {
    rc = fwrite(line, 1, n, f) == n && fputc('\n', f) != EOF ? 0 : -1;
    if(fclose(f))
      rc = -1;
  }
  free(line);
  if(++appended >= HISTORY_REWRITE_AT - HISTORY_MAX && h->n == HISTORY_MAX) {
    appended = 0;
    rc = rewrite(h, path);
  }
  return rc;
}

int history_clear(history *h, const char *path)
{
  int i;
  for(i = 0; i < h->n; i++)
    entry_free(&h->e[i]);
  h->n = 0;
  remove(path);
  return 0;
}

void history_free(history *h)
{
  int i;
  for(i = 0; i < h->n; i++)
    entry_free(&h->e[i]);
  free(h->e);
  h->e = NULL;
  h->n = 0;
}
