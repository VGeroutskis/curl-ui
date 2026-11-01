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

int args_write_config(const arg_list *l, const char *path, char *err, size_t n)
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
    if(value && strlen(flag) + 1 + put_quoted(NULL, value) >= ARG_CONFIG_LINE_MAX) {
      snprintf(err, n, "%s is over the %u MB curl reads from a config file", flag,
               ARG_CONFIG_LINE_MAX / (1024u * 1024u));
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
