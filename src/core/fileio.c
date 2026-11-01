#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "fileio.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

void file_make_parents(const char *path)
{
  size_t n = strlen(path) + 1;
  char *tmp = malloc(n);
  char *p;
  if(!tmp)
    return;
  memcpy(tmp, path, n);
  for(p = tmp + 1; *p; p++) {
    if(*p == '/' || *p == '\\') {
      char c = *p;
      *p = '\0';
#ifdef _WIN32
      _mkdir(tmp);
#else
      mkdir(tmp, 0700);
#endif
      *p = c;
    }
  }
  free(tmp);
}

FILE *file_open_private(const char *tmp)
{
#ifdef _WIN32
  return fopen(tmp, "wb");
#else
  int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0600);
  return fd < 0 ? NULL : fdopen(fd, "wb");
#endif
}

int file_commit(FILE *f, const char *tmp, const char *path)
{
  int bad = ferror(f);
  if(fclose(f) || bad) {
    remove(tmp);
    return -1;
  }
#ifdef _WIN32
  remove(path);  /* rename does not replace on Windows */
#endif
  if(rename(tmp, path)) {
    remove(tmp);
    return -1;
  }
  return 0;
}

int file_write_private(const char *path, const char *data, size_t len)
{
  size_t n = strlen(path) + 8;
  char *tmp = malloc(n);
  FILE *f;
  int rc = -1;
  if(!tmp)
    return -1;
  snprintf(tmp, n, "%s.tmp", path);
  file_make_parents(path);
  f = file_open_private(tmp);
  if(f) {
    if(len && fwrite(data, 1, len, f) != len) {
      fclose(f);
      remove(tmp);
    }
    else
      rc = file_commit(f, tmp, path);
  }
  free(tmp);
  return rc;
}

char *file_read_all(const char *path, size_t *len, size_t max)
{
  FILE *f = fopen(path, "rb");
  char *d = NULL;
  size_t n = 0, cap = 0;
  if(!f)
    return NULL;
  for(;;) {
    size_t got;
    if(n + 4096 + 1 > cap) {
      char *g;
      cap = cap ? cap * 2 : 8192;
      g = realloc(d, cap);
      if(!g)
        goto fail;
      d = g;
    }
    got = fread(d + n, 1, cap - n - 1, f);
    n += got;
    if(n > max)
      goto fail;
    if(!got)
      break;
  }
  if(ferror(f))
    goto fail;
  fclose(f);
  d[n] = '\0';
  if(len)
    *len = n;
  return d;
fail:
  free(d);
  fclose(f);
  return NULL;
}

int file_quarantine(const char *path, char *moved_to, size_t n)
{
  int i;
  for(i = 1; i < 1000; i++) {
    FILE *f;
    if((size_t)snprintf(moved_to, n, "%s.bad-%d", path, i) >= n)
      return -1;
    f = fopen(moved_to, "rb");
    if(f) {
      fclose(f);
      continue;
    }
    return rename(path, moved_to) ? -1 : 0;
  }
  return -1;
}
