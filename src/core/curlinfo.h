/* What the installed curl is, and which options it knows. */
#ifndef CURLUI_CURLINFO_H
#define CURLUI_CURLINFO_H

typedef struct {
  int found;              /* `curl --version` ran and looked like curl */
  char version[32];       /* "8.18.0" */
  char banner[256];       /* first line of --version */
  int major, minor, patch;
  char **names;           /* sorted long names from --help all */
  int nnames;
} curl_info;

/* Runs the program (blocking, killed after a few seconds). Returns found. */
int curlinfo_detect(const char *program, curl_info *info);
void curlinfo_free(curl_info *info);
/* 1 when listed, or when nothing could be determined: never cry wolf. */
int curlinfo_supports(const curl_info *info, const char *name);
int curlinfo_at_least(const curl_info *info, int major, int minor);
/* Parsers, separate for tests. */
void curlinfo_parse_version(const char *text, curl_info *info);
void curlinfo_parse_help(const char *text, curl_info *info);

#endif
