#include "outbuf.h"
#include "osthread.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* What a control byte or invalid UTF-8 sequence becomes. */
#define REPLACEMENT "\xC2\xB7" /* middle dot */
#define TAB_SPACES "    "

struct outbuf {
  os_mutex lock;
  size_t cap;
  char *data;
  size_t size, data_alloc;
  size_t *lines;       /* start offset of each line */
  size_t nlines, lines_alloc;
  size_t total_in;
  int truncated;
  int pending_cr;
  unsigned char partial[4]; /* UTF-8 sequence split across appends */
  int partial_len, partial_need;
  unsigned long generation;
  char marker_prefix[32];
  size_t marker_len;
  char marker[OUTBUF_MARKER_CAP];
  int has_marker;
};

static void reset(outbuf *b)
{
  b->size = 0;
  b->nlines = 0;
  b->total_in = 0;
  b->truncated = 0;
  b->pending_cr = 0;
  b->partial_len = 0;
  b->partial_need = 0;
  b->has_marker = 0;
  b->generation++;
}

outbuf *outbuf_new(size_t cap)
{
  outbuf *b = calloc(1, sizeof(*b));
  if(!b)
    return NULL;
  os_mutex_init(&b->lock);
  b->cap = cap ? cap : OUTBUF_CAP;
  return b;
}

void outbuf_free(outbuf *b)
{
  if(!b)
    return;
  os_mutex_destroy(&b->lock);
  free(b->data);
  free(b->lines);
  free(b);
}

void outbuf_clear(outbuf *b)
{
  os_mutex_lock(&b->lock);
  reset(b);
  os_mutex_unlock(&b->lock);
}

void outbuf_set_marker(outbuf *b, const char *prefix)
{
  os_mutex_lock(&b->lock);
  snprintf(b->marker_prefix, sizeof(b->marker_prefix), "%s", prefix ? prefix : "");
  b->marker_len = strlen(b->marker_prefix);
  os_mutex_unlock(&b->lock);
}

static int add_line(outbuf *b)
{
  if(b->nlines == b->lines_alloc) {
    size_t n = b->lines_alloc ? b->lines_alloc * 2 : 1024;
    size_t *grown = realloc(b->lines, n * sizeof(size_t));
    if(!grown)
      return -1;
    b->lines = grown;
    b->lines_alloc = n;
  }
  b->lines[b->nlines++] = b->size;
  return 0;
}

/* Appends one complete character. */
static void put_char(outbuf *b, const char *s, size_t n)
{
  if(b->truncated)
    return;
  if(b->size + n > b->cap) {
    b->truncated = 1;
    return;
  }
  if(b->size + n > b->data_alloc) {
    size_t alloc = b->data_alloc ? b->data_alloc * 2 : 65536;
    char *grown;
    if(alloc > b->cap)
      alloc = b->cap;
    grown = realloc(b->data, alloc);
    if(!grown) {
      b->truncated = 1;
      return;
    }
    b->data = grown;
    b->data_alloc = alloc;
  }
  if(!b->nlines && add_line(b))
    return;
  if(b->size - b->lines[b->nlines - 1] + n > OUTBUF_MAX_LINE) {
    if(add_line(b))
      return;
  }
  memcpy(b->data + b->size, s, n);
  b->size += n;
}

/* Current line is complete: divert it if it is a marker line. Returns 1 when
   the line was removed, leaving the slot ready for reuse. */
static int take_marker(outbuf *b)
{
  size_t start, len;
  if(!b->marker_len || !b->nlines)
    return 0;
  start = b->lines[b->nlines - 1];
  len = b->size - start;
  if(len < b->marker_len || memcmp(b->data + start, b->marker_prefix, b->marker_len))
    return 0;
  len -= b->marker_len;
  if(len >= sizeof(b->marker))
    len = sizeof(b->marker) - 1;
  memcpy(b->marker, b->data + start + b->marker_len, len);
  b->marker[len] = '\0';
  b->has_marker = 1;
  b->size = start;
  return 1;
}

static void end_line(outbuf *b)
{
  if(b->truncated)
    return;
  if(!b->nlines)
    add_line(b);
  if(take_marker(b))
    return;
  /* The next character opens a new line lazily: record an empty start. */
  add_line(b);
}

static void cr_overwrite(outbuf *b)
{
  /* lone \r: progress-bar style rewrite of the current line */
  if(b->nlines)
    b->size = b->lines[b->nlines - 1];
}

static void flush_partial_invalid(outbuf *b)
{
  if(b->partial_len) {
    put_char(b, REPLACEMENT, 2);
    b->partial_len = 0;
    b->partial_need = 0;
  }
}

static int utf8_lead_len(unsigned char c)
{
  if(c >= 0xC2 && c <= 0xDF)
    return 2;
  if(c >= 0xE0 && c <= 0xEF)
    return 3;
  if(c >= 0xF0 && c <= 0xF4)
    return 4;
  return 0;
}

static int utf8_second_ok(unsigned char lead, unsigned char c)
{
  if(lead == 0xE0)
    return c >= 0xA0 && c <= 0xBF;  /* no overlong */
  if(lead == 0xED)
    return c >= 0x80 && c <= 0x9F;  /* no surrogates */
  if(lead == 0xF0)
    return c >= 0x90 && c <= 0xBF;
  if(lead == 0xF4)
    return c >= 0x80 && c <= 0x8F;  /* <= U+10FFFF */
  return c >= 0x80 && c <= 0xBF;
}

void outbuf_append(outbuf *b, const char *data, size_t len)
{
  size_t i;
  os_mutex_lock(&b->lock);
  b->total_in += len;
  for(i = 0; i < len; i++) {
    unsigned char c = (unsigned char)data[i];
    if(b->partial_len) {
      int ok = (b->partial_len == 1) ? utf8_second_ok(b->partial[0], c)
                                     : (c >= 0x80 && c <= 0xBF);
      if(ok) {
        b->partial[b->partial_len++] = c;
        if(b->partial_len == b->partial_need) {
          put_char(b, (const char *)b->partial, (size_t)b->partial_need);
          b->partial_len = 0;
          b->partial_need = 0;
        }
        continue;
      }
      flush_partial_invalid(b);  /* and handle c normally */
    }
    if(b->pending_cr) {
      b->pending_cr = 0;
      if(c == '\n') {
        end_line(b);
        continue;
      }
      cr_overwrite(b);
    }
    if(c == '\r')
      b->pending_cr = 1;
    else if(c == '\n')
      end_line(b);
    else if(c == '\t')
      put_char(b, TAB_SPACES, 4);
    else if(c < 0x20 || c == 0x7F)
      put_char(b, REPLACEMENT, 2);
    else if(c < 0x80)
      put_char(b, (const char *)&c, 1);
    else {
      int need = utf8_lead_len(c);
      if(need) {
        b->partial[0] = c;
        b->partial_len = 1;
        b->partial_need = need;
      }
      else
        put_char(b, REPLACEMENT, 2);
    }
  }
  b->generation++;
  os_mutex_unlock(&b->lock);
}

int outbuf_get_marker(outbuf *b, char *dst, size_t dstlen)
{
  int has;
  os_mutex_lock(&b->lock);
  has = b->has_marker;
  if(has && dstlen)
    snprintf(dst, dstlen, "%s", b->marker);
  os_mutex_unlock(&b->lock);
  return has;
}

/* A trailing empty line only means "the last line ended with \n". */
size_t outbuf_line_count_locked(outbuf *b)
{
  if(b->nlines && b->lines[b->nlines - 1] == b->size)
    return b->nlines - 1;
  return b->nlines;
}

const char *outbuf_line_locked(outbuf *b, size_t index, size_t *len)
{
  size_t end;
  if(index >= b->nlines) {
    *len = 0;
    return "";
  }
  end = (index + 1 < b->nlines) ? b->lines[index + 1] : b->size;
  *len = end - b->lines[index];
  return b->data + b->lines[index];
}

void outbuf_lock(outbuf *b) { os_mutex_lock(&b->lock); }
void outbuf_unlock(outbuf *b) { os_mutex_unlock(&b->lock); }

size_t outbuf_line_count(outbuf *b)
{
  size_t n;
  os_mutex_lock(&b->lock);
  n = outbuf_line_count_locked(b);
  os_mutex_unlock(&b->lock);
  return n;
}

size_t outbuf_total_in(outbuf *b)
{
  size_t n;
  os_mutex_lock(&b->lock);
  n = b->total_in;
  os_mutex_unlock(&b->lock);
  return n;
}

int outbuf_truncated(outbuf *b)
{
  int t;
  os_mutex_lock(&b->lock);
  t = b->truncated;
  os_mutex_unlock(&b->lock);
  return t;
}

unsigned long outbuf_generation(outbuf *b)
{
  unsigned long g;
  os_mutex_lock(&b->lock);
  g = b->generation;
  os_mutex_unlock(&b->lock);
  return g;
}

size_t outbuf_get_line(outbuf *b, size_t index, char *dst, size_t dstlen)
{
  size_t len = 0;
  const char *s;
  os_mutex_lock(&b->lock);
  s = outbuf_line_locked(b, index, &len);
  if(dstlen) {
    if(len >= dstlen)
      len = dstlen - 1;
    memcpy(dst, s, len);
    dst[len] = '\0';
  }
  os_mutex_unlock(&b->lock);
  return len;
}

char *outbuf_join(outbuf *b, size_t *out_len)
{
  char *out;
  size_t n = 0, i, count;
  os_mutex_lock(&b->lock);
  count = outbuf_line_count_locked(b);
  out = malloc(b->size + count + 1);
  if(out) {
    for(i = 0; i < count; i++) {
      size_t len;
      const char *s = outbuf_line_locked(b, i, &len);
      memcpy(out + n, s, len);
      n += len;
      if(i + 1 < count)
        out[n++] = '\n';
    }
    out[n] = '\0';
  }
  os_mutex_unlock(&b->lock);
  if(out_len)
    *out_len = n;
  return out;
}
