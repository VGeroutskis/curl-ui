/* The query of a URL: percent encoding, and rows of key and value. */
#ifndef CURLUI_URLQUERY_H
#define CURLUI_URLQUERY_H

char *pct_encode(const char *s);  /* keeps A-Za-z0-9-_.~; caller frees */

#endif
