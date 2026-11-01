/* Thread-safe, line-indexed output buffer. Input is arbitrary bytes; what is
   stored is always valid UTF-8 with no control characters. */
#ifndef CURLUI_OUTBUF_H
#define CURLUI_OUTBUF_H

#include <stddef.h>

#define OUTBUF_CAP (8u * 1024u * 1024u)
#define OUTBUF_MAX_LINE 4096u   /* longer lines split at a character boundary */
#define OUTBUF_MARKER_CAP 512

typedef struct outbuf outbuf;

outbuf *outbuf_new(size_t cap);  /* 0 selects OUTBUF_CAP */
void outbuf_free(outbuf *b);
void outbuf_clear(outbuf *b);
void outbuf_append(outbuf *b, const char *data, size_t len);
/* Whole lines starting with prefix are held back from the text and kept in a
   slot instead: how the UI gets curl's --write-out stats. */
void outbuf_set_marker(outbuf *b, const char *prefix);
/* Copies the last marker line (without prefix) into dst; 0 if there is none. */
int outbuf_get_marker(outbuf *b, char *dst, size_t dstlen);

size_t outbuf_line_count(outbuf *b);
size_t outbuf_total_in(outbuf *b);    /* raw bytes given to append */
int outbuf_truncated(outbuf *b);
unsigned long outbuf_generation(outbuf *b);  /* changes with content */
/* Copies a line, NUL-terminated and cut to fit; returns the bytes copied. */
size_t outbuf_get_line(outbuf *b, size_t index, char *dst, size_t dstlen);
/* All lines joined by \n. Caller frees. */
char *outbuf_join(outbuf *b, size_t *len);

/* Zero-copy access: pointers are valid only until outbuf_unlock(). */
void outbuf_lock(outbuf *b);
void outbuf_unlock(outbuf *b);
size_t outbuf_line_count_locked(outbuf *b);
const char *outbuf_line_locked(outbuf *b, size_t index, size_t *len);

#endif
