#include "urlquery.h"

#include <stdlib.h>
#include <string.h>

static int unreserved(unsigned char c)
{
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
         c == '-' || c == '_' || c == '.' || c == '~';
}

char *pct_encode(const char *s)
{
  static const char hex[] = "0123456789ABCDEF";
  char *out = malloc(strlen(s) * 3 + 1), *d = out;
  if(!out)
    return NULL;
  for(; *s; s++) {
    unsigned char c = (unsigned char)*s;
    if(unreserved(c))
      *d++ = (char)c;
    else {
      *d++ = '%';
      *d++ = hex[c >> 4];
      *d++ = hex[c & 15];
    }
  }
  *d = '\0';
  return out;
}
