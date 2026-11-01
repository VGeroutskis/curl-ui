#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "argfile.h"
#include "fileio.h"
#include "options.h"
#include "procrun.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#endif

int args_need_file(const char *const *argv)
{
#ifdef _WIN32
  char *line = proc_win_cmdline(argv);
  size_t n = line ? strlen(line) : (size_t)-1;
  free(line);
  return n > ARG_SPILL_WIN_CMDLINE;
#else
  size_t total = 0;
  int i;
  for(i = 0; argv[i]; i++) {
    size_t n = strlen(argv[i]);
    if(n > ARG_SPILL_ONE)
      return 1;
    total += n + 1;
  }
  return total > ARG_SPILL_TOTAL;
#endif
}

/* Returns the characters written, the same count when f is NULL. */
static size_t put_quoted(FILE *f, const char *s)
{
  size_t n = 2;
  if(f)
    fputc('"', f);
  for(; *s; s++) {
    const char *esc = NULL;
    switch(*s) {
    case '\\': esc = "\\\\"; break;
    case '"': esc = "\\\""; break;
    case '\t': esc = "\\t"; break;
    case '\n': esc = "\\n"; break;
    case '\r': esc = "\\r"; break;
    case '\v': esc = "\\v"; break;
    default: break;
    }
    if(esc) {
      if(f)
        fputs(esc, f);
      n += 2;
    }
    else {
      if(f)
        fputc(*s, f);
      n++;
    }
  }
  if(f)
    fputc('"', f);
  return n;
}

int args_spill_bodies(arg_list *l, const char *stem, arg_files *out, char *err, size_t n)
{
  static const char *const bodies[] = { "data", "data-ascii", "data-raw", "data-binary", "json", NULL };
  int raw = options_find("data-raw"), json = options_find("json");
  int i, b;
  for(i = 1; i + 1 < l->count; i++) {
    arg_token *t = &l->tok[i], *v = &l->tok[i + 1];
    size_t vn, pn;
    char *path, *value, *flag = NULL, **grown;
    int is_body = 0;
    if(t->kind != TOK_FLAG || v->kind != TOK_VALUE)
      continue;
    for(b = 0; bodies[b]; b++)
      if(t->opt == options_find(bodies[b]))
        is_body = 1;
    vn = strlen(v->text);
    if(!is_body || vn <= ARG_BODY_SPILL || (v->text[0] == '@' && t->opt != raw))
      continue;
    pn = strlen(stem) + 32;
    path = malloc(pn);
    value = malloc(pn + 1);
    grown = realloc(out->paths, sizeof(char *) * (size_t)(out->n + 1));
    if(t->opt != json) {
      flag = malloc(16);
      if(flag)
        memcpy(flag, "--data-binary", 14);
    }
    if(grown)
      out->paths = grown;
    if(!path || !value || !grown || (t->opt != json && !flag)) {
      free(path);
      free(value);
      free(flag);
      snprintf(err, n, "out of memory");
      return -1;
    }
    snprintf(path, pn, "%s-b%d.conf", stem, out->n);
    if(file_write_private(path, v->text, vn)) {
      snprintf(err, n, "cannot write %s", path);
      free(path);
      free(value);
      free(flag);
      return -1;
    }
    snprintf(value, pn + 1, "@%s", path);
    out->paths[out->n++] = path;
    free(v->text);
    v->text = value;
    if(flag) {
      free(t->text);
      t->text = flag;
    }
    i++;
  }
  return 0;
}

void args_files_remove(arg_files *f)
{
  int i;
  for(i = 0; i < f->n; i++) {
    remove(f->paths[i]);
    free(f->paths[i]);
  }
  free(f->paths);
  f->paths = NULL;
  f->n = 0;
}

int args_write_config(const arg_list *l, const char *path, size_t line_max, char *err, size_t n)
{
  size_t tn = strlen(path) + 8;
  char *tmp = malloc(tn);
  FILE *f;
  int i, disable = options_find("disable");
  if(!tmp)
    return -1;
  /* nothing is written unless every line fits */
  for(i = 1; i < l->count; i++) {
    const arg_token *t = &l->tok[i];
    const arg_token *v = i + 1 < l->count && t->kind == TOK_FLAG && l->tok[i + 1].kind != TOK_FLAG ? t + 1 : NULL;
    const char *value = t->kind == TOK_FLAG ? (v ? v->text : NULL) : t->text;
    const char *flag = t->kind == TOK_FLAG ? t->text : "--url";
    if(value && strlen(flag) + 1 + put_quoted(NULL, value) >= line_max) {
      snprintf(err, n, "%s is over the %.1f MB this curl can take for it", flag,
               (double)line_max / (1024.0 * 1024.0));
      free(tmp);
      return -1;
    }
    if(v)
      i++;
  }
  snprintf(tmp, tn, "%s.tmp", path);
  file_make_parents(path);
  f = file_open_private(tmp);
  if(!f) {
    snprintf(err, n, "cannot write %s", path);
    free(tmp);
    return -1;
  }
  for(i = 1; i < l->count; i++) {
    const arg_token *t = &l->tok[i];
    if(t->kind == TOK_FLAG) {
      const arg_token *v = i + 1 < l->count && l->tok[i + 1].kind != TOK_FLAG ? t + 1 : NULL;
      if(t->opt == disable && !v)
        continue;
      fputs(t->text, f);
      if(v) {
        fputc(' ', f);
        put_quoted(f, v->text);
        i++;
      }
      fputc('\n', f);
    }
    else {
      fputs("--url ", f);
      put_quoted(f, t->text);
      fputc('\n', f);
    }
  }
  i = file_commit(f, tmp, path);
  free(tmp);
  if(i)
    snprintf(err, n, "cannot write %s", path);
  return i;
}

void args_remove_stale(const char *dir)
{
  char path[2048];
#ifdef _WIN32
  WIN32_FIND_DATAA d;
  HANDLE h;
  snprintf(path, sizeof(path), "%s\\run-*.conf", dir);
  h = FindFirstFileA(path, &d);
  if(h == INVALID_HANDLE_VALUE)
    return;
  do {
    snprintf(path, sizeof(path), "%s\\%s", dir, d.cFileName);
    remove(path);
  } while(FindNextFileA(h, &d));
  FindClose(h);
#else
  DIR *d = opendir(dir);
  struct dirent *e;
  if(!d)
    return;
  while((e = readdir(d))) {
    size_t n = strlen(e->d_name);
    if(n > 9 && !strncmp(e->d_name, "run-", 4) && !strcmp(e->d_name + n - 5, ".conf")) {
      snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
      remove(path);
    }
  }
  closedir(d);
#endif
}
