#include "options.h"

#include <ctype.h>
#include <string.h>

int options_find_n(const char *name, int len)
{
  int lo = 0, hi = g_option_count - 1;
  while(len > 0 && *name == '-') {
    name++;
    len--;
  }
  if(len <= 0)
    return -1;
  while(lo <= hi) {
    int mid = (lo + hi) / 2;
    int c = strncmp(g_options[mid].name, name, (size_t)len);
    if(c == 0 && g_options[mid].name[len] != '\0')
      c = 1;
    if(c == 0)
      return mid;
    if(c < 0)
      lo = mid + 1;
    else
      hi = mid - 1;
  }
  return -1;
}

int options_find(const char *name)
{
  return options_find_n(name, (int)strlen(name));
}

int options_in_category(const opt_def *o, const char *category)
{
  size_t n = strlen(category);
  const char *p = o->categories;
  while((p = strstr(p, category)) != NULL) {
    int starts = (p == o->categories) || p[-1] == ' ';
    if(starts && (p[n] == ' ' || p[n] == '\0'))
      return 1;
    p += n;
  }
  return 0;
}

static int contains_nocase(const char *hay, const char *needle)
{
  size_t n = strlen(needle);
  if(!n)
    return 1;
  for(; *hay; hay++) {
    size_t i = 0;
    while(i < n && hay[i] &&
          tolower((unsigned char)hay[i]) == tolower((unsigned char)needle[i]))
      i++;
    if(i == n)
      return 1;
  }
  return 0;
}

/* Options a newcomer reaches for first, on top of curl's "important" set. */
static const char *const popular_extra[] = {
  "data-binary", "form", "json", "max-time", "connect-timeout", "retry",
  "compressed", "cookie", "cookie-jar", "proxy", "cacert", "http2",
  "write-out", "referer", "range", "upload-file", NULL
};

static int matches_filter(const opt_def *o, const char *filter, int searching)
{
  if(!filter || !*filter || !strcmp(filter, OPT_FILTER_ALL))
    return searching || !o->deprecated;
  if(!strcmp(filter, OPT_FILTER_POPULAR)) {
    int i;
    if(options_in_category(o, "important"))
      return 1;
    for(i = 0; popular_extra[i]; i++)
      if(!strcmp(o->name, popular_extra[i]))
        return 1;
    return 0;
  }
  return options_in_category(o, filter);
}

int options_search(const char *query, const char *filter, int *out)
{
  int i, count = 0, pass;
  const char *q = query ? query : "";
  int searching;
  while(*q == '-')
    q++;  /* "--user" and "user" search alike */
  searching = *q != '\0';
  for(pass = 0; pass < 2; pass++) {
    for(i = 0; i < g_option_count; i++) {
      const opt_def *o = &g_options[i];
      int by_name;
      if(!matches_filter(o, filter, searching))
        continue;
      by_name = contains_nocase(o->name, q) ||
                (o->short_flag[0] && !q[1] && *q == o->short_flag[0]);
      if(pass == 0 && by_name)
        out[count++] = i;
      else if(pass == 1 && !by_name &&
              (contains_nocase(o->help, q) || contains_nocase(o->description, q)))
        out[count++] = i;
    }
    if(!searching)
      break;
  }
  return count;
}
