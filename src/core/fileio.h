/* Files that can hold passwords: private to the user and replaced whole, so a crash
   cannot leave half of one. */
#ifndef CURLUI_FILEIO_H
#define CURLUI_FILEIO_H

#include <stddef.h>
#include <stdio.h>

void file_make_parents(const char *path);       /* mkdir -p for what holds path */
FILE *file_open_private(const char *tmp);       /* for writing, 0600 */
int file_commit(FILE *f, const char *tmp, const char *path);  /* closes f, moves tmp over path */

int file_write_private(const char *path, const char *data, size_t len);
/* NUL-terminated, caller frees. NULL if missing, unreadable or larger than max. */
char *file_read_all(const char *path, size_t *len, size_t max);
/* Moves path to path.bad-N, never over something that exists. */
int file_quarantine(const char *path, char *moved_to, size_t n);

#endif
