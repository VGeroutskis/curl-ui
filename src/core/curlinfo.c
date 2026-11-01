#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "curlinfo.h"
#include "osthread.h"
#include "procrun.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
static void nap_ms(int ms) { Sleep((DWORD)ms); }
#else
static void nap_ms(int ms)
{
  struct timespec ts;
  ts.tv_sec = 0;
  ts.tv_nsec = (long)ms * 1000000L;
  nanosleep(&ts, NULL);
}
#endif

#define DETECT_TIMEOUT_MS 5000
#define DETECT_CAP (1024u * 1024u)

typedef struct {
  char *buf;
  size_t n, cap;
} capture;

static void cap_sink(void *user, const char *data, size_t len)
{
  capture *c = user;
  if(c->n + len >= c->cap)
    len = c->cap - c->n - 1;
  memcpy(c->buf + c->n, data, len);
  c->n += len;
  c->buf[c->n] = '\0';
}

static void drop_sink(void *user, const char *data, size_t len)
{
  (void)user;
  (void)data;
  (void)len;
}

static char *run_capture(const char *program, const char *a1, const char *a2)
{
  const char *argv[4];
  capture c;
  char err[256];
  proc_t *p;
  int waited = 0;

  argv[0] = program;
  argv[1] = a1;
  argv[2] = a2;
  argv[3] = NULL;
  c.buf = malloc(DETECT_CAP);
  if(!c.buf)
    return NULL;
  c.n = 0;
  c.cap = DETECT_CAP;
  c.buf[0] = '\0';
  p = proc_start(argv, cap_sink, drop_sink, &c, err, sizeof(err));
  if(!p) {
    free(c.buf);
    return NULL;
  }
  while(!proc_finished(p) && waited < DETECT_TIMEOUT_MS) {
    nap_ms(5);
    waited += 5;
  }
  proc_free(p);
  return c.buf;
}

void curlinfo_parse_version(const char *text, curl_info *info)
{
  const char *eol;
  size_t n;
  int maj = 0, min = 0, pat = 0;
  info->found = 0;
  if(strncmp(text, "curl ", 5))
    return;
  eol = strpbrk(text, "\r\n");
  n = eol ? (size_t)(eol - text) : strlen(text);
  if(n >= sizeof(info->banner))
    n = sizeof(info->banner) - 1;
  memcpy(info->banner, text, n);
  info->banner[n] = '\0';
  if(sscanf(text + 5, "%d.%d.%d", &maj, &min, &pat) < 2)
    return;
  info->major = maj;
  info->minor = min;
  info->patch = pat;
  sscanf(text + 5, "%31s", info->version);
  info->found = 1;
}

static int cmp_name(const void *a, const void *b)
{
  return strcmp(*(char *const *)a, *(char *const *)b);
}

void curlinfo_parse_help(const char *text, curl_info *info)
{
  /* each line reads: " -A, --user-agent <name>   Send User-Agent..." */
  const char *line = text;
  int cap = 0;
  while(*line) {
    const char *eol = strchr(line, '\n');
    const char *end = eol ? eol : line + strlen(line);
    const char *p = line;
    while(p < end && *p == ' ')
      p++;
    if(p < end && *p == '-') {
      const char *dd = p;
      while(dd + 1 < end && !(dd[0] == '-' && dd[1] == '-') && dd[0] != ' ')
        dd++;
      if(dd[0] == ' ' && dd + 3 < end && dd[1] == '-' && dd[2] == '-')
        dd++;
      if(dd + 2 < end && dd[0] == '-' && dd[1] == '-') {
        const char *s = dd + 2, *e = s;
        while(e < end && *e != ' ' && *e != ',' && *e != '\r')
          e++;
        if(e > s) {
          char *name = malloc((size_t)(e - s) + 1);
          if(name) {
            memcpy(name, s, (size_t)(e - s));
            name[e - s] = '\0';
            if(info->nnames == cap) {
              int ncap = cap ? cap * 2 : 512;
              char **grown = realloc(info->names, sizeof(char *) * (size_t)ncap);
              if(!grown) {
                free(name);
                break;
              }
              info->names = grown;
              cap = ncap;
            }
            info->names[info->nnames++] = name;
          }
        }
      }
    }
    if(!eol)
      break;
    line = eol + 1;
  }
  if(info->names)
    qsort(info->names, (size_t)info->nnames, sizeof(char *), cmp_name);
}

int curlinfo_detect(const char *program, curl_info *info)
{
  char *text;
  memset(info, 0, sizeof(*info));
  text = run_capture(program, "--version", NULL);
  if(!text)
    return 0;
  curlinfo_parse_version(text, info);
  free(text);
  if(!info->found)
    return 0;
  text = run_capture(program, "--help", "all");
  if(text) {
    curlinfo_parse_help(text, info);
    free(text);
  }
  return 1;
}

void curlinfo_free(curl_info *info)
{
  int i;
  for(i = 0; i < info->nnames; i++)
    free(info->names[i]);
  free(info->names);
  memset(info, 0, sizeof(*info));
}

int curlinfo_supports(const curl_info *info, const char *name)
{
  int lo = 0, hi = info->nnames - 1;
  if(!info->found || info->nnames < 20)
    return 1;  /* detection failed or looks wrong: stay quiet */
  while(lo <= hi) {
    int mid = (lo + hi) / 2;
    int c = strcmp(info->names[mid], name);
    if(!c)
      return 1;
    if(c < 0)
      lo = mid + 1;
    else
      hi = mid - 1;
  }
  return 0;
}

int curlinfo_at_least(const curl_info *info, int major, int minor)
{
  if(!info->found)
    return 0;
  return info->major > major || (info->major == major && info->minor >= minor);
}
