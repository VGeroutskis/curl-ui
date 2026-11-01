#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "settings.h"
#include "fileio.h"
#include "options.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LINE_CAP (BUILDER_VALUE_CAP * 2 + 64)

void settings_defaults(settings_t *s)
{
  memset(s, 0, sizeof(*s));
  s->dark = 1;
  s->zoom = 0;
  snprintf(s->program, sizeof(s->program), "curl");
  s->pretty = 1;
  s->capture = 1;
  s->hover_ms = 110;
}

int settings_path(const char *override, const char *name, char *out, size_t n)
{
  const char *base = override;
  const char *suffix = "";
  int len;
  if(!base || !*base) {
#ifdef _WIN32
    base = getenv("APPDATA");
    suffix = "\\curl-ui";
#elif defined(__APPLE__)
    base = getenv("HOME");
    suffix = "/Library/Application Support/curl-ui";
#else
    base = getenv("XDG_CONFIG_HOME");
    suffix = "/curl-ui";
    if(!base || !*base) {
      base = getenv("HOME");
      suffix = "/.config/curl-ui";
    }
#endif
  }
  if(!base || !*base)
    return -1;
#ifdef _WIN32
  len = snprintf(out, n, "%s%s\\%s", base, suffix, name);
#else
  len = snprintf(out, n, "%s%s/%s", base, suffix, name);
#endif
  return (len < 0 || (size_t)len >= n) ? -1 : 0;
}

/* One value per line: newlines and backslashes are escaped. */
static void write_escaped(FILE *f, const char *key, const char *v)
{
  fprintf(f, "%s=", key);
  for(; *v; v++) {
    if(*v == '\\')
      fputs("\\\\", f);
    else if(*v == '\n')
      fputs("\\n", f);
    else if(*v == '\r')
      fputs("\\r", f);
    else
      fputc(*v, f);
  }
  fputc('\n', f);
}

static void unescape(char *s)
{
  char *d = s;
  for(; *s; s++) {
    if(*s == '\\' && s[1]) {
      s++;
      *d++ = *s == 'n' ? '\n' : (*s == 'r' ? '\r' : *s);
    }
    else
      *d++ = *s;
  }
  *d = '\0';
}

/* Next "key=value" line; comments and junk are skipped. 0 at end of file. */
static int next_pair(FILE *f, char *line, size_t cap, char **key, char **value)
{
  while(fgets(line, (int)cap, f)) {
    size_t n = strlen(line);
    char *eq;
    while(n && (line[n - 1] == '\n' || line[n - 1] == '\r'))
      line[--n] = '\0';
    if(!n || line[0] == '#')
      continue;
    eq = strchr(line, '=');
    if(!eq)
      continue;
    *eq = '\0';
    *key = line;
    *value = eq + 1;
    return 1;
  }
  return 0;
}

static int parse_int(const char *v, int lo, int hi, int fallback)
{
  char *end;
  long n = strtol(v, &end, 10);
  if(end == v || *end || n < lo || n > hi)
    return fallback;
  return (int)n;
}

int settings_load(settings_t *s, const char *path)
{
  FILE *f = fopen(path, "rb");
  char *line, *k, *v;
  if(!f)
    return 0;
  line = malloc(LINE_CAP);
  if(!line) {
    fclose(f);
    return 0;
  }
  while(next_pair(f, line, LINE_CAP, &k, &v)) {
    if(!strcmp(k, "theme"))
      s->dark = strcmp(v, "light") != 0;
    else if(!strcmp(k, "text_size")) {
      int z = parse_int(v, 0, 400, -1);
      if(z == 0 || (z >= 50 && z <= 400))
        s->zoom = z;
    }
    else if(!strcmp(k, "curl_program") && *v) {
      unescape(v);
      snprintf(s->program, sizeof(s->program), "%s", v);
    }
    else if(!strcmp(k, "curl_config")) {
      unescape(v);
      snprintf(s->config_file, sizeof(s->config_file), "%s", v);
    }
    else if(!strcmp(k, "ignore_curlrc"))
      s->no_curlrc = parse_int(v, 0, 1, s->no_curlrc);
    else if(!strcmp(k, "pretty_json"))
      s->pretty = parse_int(v, 0, 1, s->pretty);
    else if(!strcmp(k, "capture_log"))
      s->capture = parse_int(v, 0, 1, s->capture);
    else if(!strcmp(k, "docs_hover_ms"))
      s->hover_ms = parse_int(v, 0, 2000, s->hover_ms);
    else if(!strcmp(k, "remember_request"))
      s->remember = parse_int(v, 0, 1, s->remember);
    else if(!strcmp(k, "window_width"))
      s->win_w = parse_int(v, 0, 32768, s->win_w);
    else if(!strcmp(k, "window_height"))
      s->win_h = parse_int(v, 0, 32768, s->win_h);
  }
  free(line);
  fclose(f);
  return 1;
}

int settings_save(const settings_t *s, const char *path)
{
  char tmp[SETTINGS_PATH_CAP + 8];
  FILE *f;
  file_make_parents(path);
  snprintf(tmp, sizeof(tmp), "%s.tmp", path);
  f = file_open_private(tmp);
  if(!f)
    return -1;
  fputs("# curl UI settings. Safe to edit while the app is closed.\n", f);
  fprintf(f, "theme=%s\n", s->dark ? "dark" : "light");
  fputs("# percent; 0 detects it from the display\n", f);
  fprintf(f, "text_size=%d\n", s->zoom);
  write_escaped(f, "curl_program", s->program);
  fputs("# a curl config file, passed as --config on every run\n", f);
  write_escaped(f, "curl_config", s->config_file);
  fprintf(f, "ignore_curlrc=%d\n", s->no_curlrc);
  fprintf(f, "pretty_json=%d\n", s->pretty);
  fprintf(f, "capture_log=%d\n", s->capture);
  fprintf(f, "docs_hover_ms=%d\n", s->hover_ms);
  fprintf(f, "remember_request=%d\n", s->remember);
  fprintf(f, "window_width=%d\n", s->win_w);
  fprintf(f, "window_height=%d\n", s->win_h);
  return file_commit(f, tmp, path);
}

int request_save(const request *r, int method, const char *path)
{
  char tmp[SETTINGS_PATH_CAP + 8];
  FILE *f;
  int i, j;
  file_make_parents(path);
  snprintf(tmp, sizeof(tmp), "%s.tmp", path);
  f = file_open_private(tmp);
  if(!f)
    return -1;
  fprintf(f, "method=%d\n", method);
  write_escaped(f, "url", r->url);
  for(i = 0; i < r->nsel; i++) {
    const sel_option *s = &r->sel[i];
    fprintf(f, "option=%s\n", g_options[s->opt].name);
    if(s->negated)
      fputs("negated=1\n", f);
    for(j = 0; j < s->nvalues; j++)
      write_escaped(f, "value", s->values[j]);
  }
  return file_commit(f, tmp, path);
}

int request_load(request *r, int *method, const char *path)
{
  FILE *f = fopen(path, "rb");
  char *line, *k, *v;
  int current = -1, first_value = 0;
  if(!f)
    return 0;
  line = malloc(LINE_CAP);
  if(!line) {
    fclose(f);
    return 0;
  }
  while(next_pair(f, line, LINE_CAP, &k, &v)) {
    if(!strcmp(k, "method"))
      *method = parse_int(v, 0, 7, 0);
    else if(!strcmp(k, "url")) {
      unescape(v);
      snprintf(r->url, sizeof(r->url), "%s", v);
    }
    else if(!strcmp(k, "option")) {
      /* an option this version does not know is dropped with its values */
      int opt = options_find(v);
      current = -1;
      if(opt >= 0 && request_add(r, opt)) {
        current = opt;
        first_value = 1;
      }
    }
    else if(current >= 0) {
      sel_option *s = &r->sel[request_find(r, current)];
      if(!strcmp(k, "negated"))
        s->negated = g_options[current].neg[0] != 0;
      else if(!strcmp(k, "value") && g_options[current].arg[0]) {
        char *slot;
        unescape(v);
        /* the first value goes into the empty field request_add made */
        slot = (first_value && s->nvalues) ? s->values[0] : request_add_value(s);
        first_value = 0;
        if(slot)
          snprintf(slot, BUILDER_VALUE_CAP, "%s", v);
      }
    }
  }
  free(line);
  fclose(f);
  return 1;
}
