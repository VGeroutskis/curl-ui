#include "forms.h"
#include "options.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void form_hints_init(form_hints *h)
{
  memset(h, 0, sizeof(*h));
}

void form_hints_free(form_hints *h)
{
  free(h->apikey_name);
  kv_free(&h->disabled_params);
  kv_free(&h->disabled_headers);
  kv_free(&h->disabled_body);
  memset(h, 0, sizeof(*h));
}

static sel_option *sel_of(const request *r, const char *name)
{
  int slot = request_find(r, options_find(name));
  return slot >= 0 ? &r->sel[slot] : NULL;
}

static void drop(request *r, const char *name)
{
  request_remove(r, options_find(name));
}

static void add_value(request *r, const char *name, const char *value)
{
  int opt = options_find(name);
  int fresh = request_find(r, opt) < 0;
  sel_option *s = request_add(r, opt);
  if(!s)
    return;
  if(fresh)
    request_set_value(s, 0, value);
  else
    request_append_value(s, value);
}

static char *join3(const char *a, const char *b, const char *c)
{
  size_t na = strlen(a), nb = strlen(b), nc = strlen(c);
  char *d = malloc(na + nb + nc + 1);
  if(d) {
    memcpy(d, a, na);
    memcpy(d + na, b, nb);
    memcpy(d + na + nb, c, nc + 1);
  }
  return d;
}

/* ---- headers ---- */

void form_headers_get(const request *r, const form_hints *h, kv_list *rows)
{
  const sel_option *s = sel_of(r, "header");
  int i;
  memset(rows, 0, sizeof(*rows));
  for(i = 0; s && i < s->nvalues; i++) {
    const char *v = s->values[i];
    const char *colon = strchr(v, ':');
    if(colon && colon > v) {
      char *name = malloc((size_t)(colon - v) + 1);
      if(!name)
        continue;
      memcpy(name, v, (size_t)(colon - v));
      name[colon - v] = '\0';
      colon++;
      while(*colon == ' ')
        colon++;
      kv_add(rows, name, colon, 1);
      free(name);
    }
    else
      kv_add(rows, v, "", 1);  /* X-Empty; and @file mean something: kept as written */
  }
  for(i = 0; i < h->disabled_headers.n; i++)
    kv_add(rows, h->disabled_headers.r[i].key, h->disabled_headers.r[i].value, 0);
}

void form_headers_set(request *r, form_hints *h, const kv_list *rows)
{
  int i;
  drop(r, "header");
  kv_free(&h->disabled_headers);
  for(i = 0; i < rows->n; i++) {
    const kv_row *row = &rows->r[i];
    if(!row->key[0] && !row->value[0])
      continue;
    if(!row->enabled)
      kv_add(&h->disabled_headers, row->key, row->value, 0);
    else if(!row->value[0])
      add_value(r, "header", row->key);
    else {
      char *line = join3(row->key, ": ", row->value);
      if(line) {
        add_value(r, "header", line);
        free(line);
      }
    }
  }
}

/* ---- body ---- */

static const char *const g_body_options[] = {
  "data", "data-ascii", "data-binary", "data-raw", "data-urlencode", "form", "form-string", "json",
  "upload-file", NULL
};

static int form_value_simple(const char *v)
{
  const char *eq = strchr(v, '=');
  if(!eq || eq == v)
    return 0;
  /* ;type=, ;filename= and <file are more than a row has room for */
  if(eq[1] == '<' || strchr(eq + 1, ';') || strchr(eq + 1, ','))
    return 0;
  return 1;
}

body_kind form_body_kind(const request *r)
{
  int kinds = 0, i;
  body_kind k = BODY_NONE;
  const sel_option *s;
  for(i = 0; g_body_options[i]; i++)
    if(sel_of(r, g_body_options[i]))
      kinds++;
  if(!kinds)
    return BODY_NONE;
  if((s = sel_of(r, "form")) != NULL || sel_of(r, "form-string")) {
    if(kinds > (s && sel_of(r, "form-string") ? 2 : 1))
      return BODY_OPTIONS;
    for(i = 0; s && i < s->nvalues; i++) {
      const char *eq = strchr(s->values[i], '=');
      if(!form_value_simple(s->values[i]) || eq[1] != '@')
        return BODY_OPTIONS;
    }
    return BODY_FORMDATA;
  }
  if(kinds > 1)
    return BODY_OPTIONS;
  if((s = sel_of(r, "data-raw")) != NULL)
    k = BODY_RAW;
  else if((s = sel_of(r, "json")) != NULL)
    k = BODY_JSON;
  else if((s = sel_of(r, "data-binary")) != NULL)
    k = BODY_FILE;
  else if((s = sel_of(r, "data-urlencode")) != NULL) {
    for(i = 0; i < s->nvalues; i++) {
      const char *v = s->values[i];
      const char *eq = strchr(v, '=');
      const char *at = strchr(v, '@');
      if(!eq || eq == v || (at && at < eq))
        return BODY_OPTIONS;
    }
    return BODY_URLENC;
  }
  else
    return BODY_OPTIONS;
  if(s->nvalues != 1)
    return BODY_OPTIONS;
  if(k == BODY_FILE && s->values[0][0] != '@')
    return BODY_OPTIONS;
  return k;
}

static char *copy_text(const char *s)
{
  size_t n = strlen(s) + 1;
  char *d = malloc(n);
  if(d)
    memcpy(d, s, n);
  return d;
}

static void split_form_rows(const sel_option *s, kv_list *rows, int is_file)
{
  int i;
  for(i = 0; s && i < s->nvalues; i++) {
    const char *eq = strchr(s->values[i], '=');
    char *name;
    kv_row *row;
    if(!eq)
      continue;
    name = malloc((size_t)(eq - s->values[i]) + 1);
    if(!name)
      continue;
    memcpy(name, s->values[i], (size_t)(eq - s->values[i]));
    name[eq - s->values[i]] = '\0';
    row = kv_add(rows, name, eq + 1 + (is_file ? 1 : 0), 1);
    if(row)
      row->is_file = is_file;
    free(name);
  }
}

int form_body_get(const request *r, kv_list *rows, char **text)
{
  const sel_option *s;
  int i;
  memset(rows, 0, sizeof(*rows));
  *text = NULL;
  switch(form_body_kind(r)) {
  case BODY_RAW:
    *text = copy_text(sel_of(r, "data-raw")->values[0]);
    break;
  case BODY_JSON:
    *text = copy_text(sel_of(r, "json")->values[0]);
    break;
  case BODY_FILE:
    *text = copy_text(sel_of(r, "data-binary")->values[0] + 1);
    break;
  case BODY_FORMDATA:
    split_form_rows(sel_of(r, "form-string"), rows, 0);
    split_form_rows(sel_of(r, "form"), rows, 1);
    break;
  case BODY_URLENC:
    s = sel_of(r, "data-urlencode");
    for(i = 0; i < s->nvalues; i++) {
      const char *eq = strchr(s->values[i], '=');
      char *raw = malloc((size_t)(eq - s->values[i]) + 1), *name;
      if(!raw)
        continue;
      memcpy(raw, s->values[i], (size_t)(eq - s->values[i]));
      raw[eq - s->values[i]] = '\0';
      name = pct_decode(raw, 0);
      free(raw);
      if(name) {
        kv_add(rows, name, eq + 1, 1);
        free(name);
      }
    }
    break;
  default:
    break;
  }
  return 0;
}

int form_body_set(request *r, body_kind k, const kv_list *rows, const char *text, char *err, size_t n)
{
  int i;
  if(form_body_kind(r) == BODY_OPTIONS) {
    snprintf(err, n, "The body is set by options the form cannot show");
    return -1;
  }
  for(i = 0; g_body_options[i]; i++)
    drop(r, g_body_options[i]);
  switch(k) {
  case BODY_RAW:
    add_value(r, "data-raw", text ? text : "");
    break;
  case BODY_JSON:
    add_value(r, "json", text ? text : "");
    break;
  case BODY_FILE: {
    char *v = join3("@", text ? text : "", "");
    if(v) {
      add_value(r, "data-binary", v);
      free(v);
    }
    break;
  }
  case BODY_FORMDATA:
    for(i = 0; rows && i < rows->n; i++) {
      const kv_row *row = &rows->r[i];
      char *v;
      if(!row->enabled || (!row->key[0] && !row->value[0]))
        continue;
      v = join3(row->key, row->is_file ? "=@" : "=", row->value);
      if(v) {
        add_value(r, row->is_file ? "form" : "form-string", v);
        free(v);
      }
    }
    break;
  case BODY_URLENC:
    for(i = 0; rows && i < rows->n; i++) {
      const kv_row *row = &rows->r[i];
      char *name, *v;
      if(!row->enabled || (!row->key[0] && !row->value[0]))
        continue;
      /* curl encodes the content; the name is sent as given */
      name = pct_encode(row->key);
      v = name ? join3(name, "=", row->value) : NULL;
      if(v)
        add_value(r, "data-urlencode", v);
      free(name);
      free(v);
    }
    break;
  default:
    break;
  }
  return 0;
}

/* ---- auth ---- */

static const char *const g_other_auth[] = {
  "anyauth", "ntlm", "ntlm-wb", "negotiate", "aws-sigv4", NULL
};

auth_kind form_auth_kind(const request *r, const form_hints *h)
{
  int user = sel_of(r, "user") != NULL, basic = sel_of(r, "basic") != NULL;
  int digest = sel_of(r, "digest") != NULL, bearer = sel_of(r, "oauth2-bearer") != NULL;
  int i;
  for(i = 0; g_other_auth[i]; i++)
    if(options_find(g_other_auth[i]) >= 0 && sel_of(r, g_other_auth[i]))
      return AUTH_OPTIONS;
  if(basic + digest > 1 || (bearer && (user || basic || digest)))
    return AUTH_OPTIONS;
  if(bearer)
    return sel_of(r, "oauth2-bearer")->nvalues == 1 ? AUTH_BEARER : AUTH_OPTIONS;
  if(digest)
    return user ? AUTH_DIGEST : AUTH_OPTIONS;
  if(user) {
    if(sel_of(r, "user")->nvalues != 1)
      return AUTH_OPTIONS;
    return AUTH_BASIC;
  }
  if(basic)
    return AUTH_OPTIONS;
  if(h && !strcmp(h->auth, "apikey"))
    return AUTH_APIKEY;
  return AUTH_NONE;
}

int form_auth_get(const request *r, const form_hints *h, char **a, char **b)
{
  *a = copy_text("");
  *b = copy_text("");
  if(!*a || !*b)
    return -1;
  switch(form_auth_kind(r, h)) {
  case AUTH_BASIC:
  case AUTH_DIGEST: {
    const char *v = sel_of(r, "user")->values[0];
    const char *colon = strchr(v, ':');
    free(*a);
    free(*b);
    if(colon) {
      *a = malloc((size_t)(colon - v) + 1);
      if(*a) {
        memcpy(*a, v, (size_t)(colon - v));
        (*a)[colon - v] = '\0';
      }
      *b = copy_text(colon + 1);
    }
    else {
      *a = copy_text(v);
      *b = copy_text("");
    }
    break;
  }
  case AUTH_BEARER:
    free(*a);
    *a = copy_text(sel_of(r, "oauth2-bearer")->values[0]);
    break;
  case AUTH_APIKEY:
    if(h->apikey_name) {
      free(*a);
      *a = copy_text(h->apikey_name);
      if(!strcmp(h->apikey_where, "header")) {
        form_hints none;
        kv_list rows;
        int i;
        form_hints_init(&none);
        form_headers_get(r, &none, &rows);
        for(i = 0; i < rows.n; i++) {
          if(!strcmp(rows.r[i].key, h->apikey_name)) {
            free(*b);
            *b = copy_text(rows.r[i].value);
          }
        }
        kv_free(&rows);
      }
      else {
        kv_list rows;
        int i;
        if(!query_parse(r->url, &rows)) {
          for(i = 0; i < rows.n; i++) {
            if(!strcmp(rows.r[i].key, h->apikey_name)) {
              free(*b);
              *b = copy_text(rows.r[i].value);
            }
          }
          kv_free(&rows);
        }
      }
    }
    break;
  default:
    break;
  }
  return 0;
}

static void apikey_clear(request *r, form_hints *h)
{
  kv_list rows;
  int i;
  if(!h->apikey_name)
    return;
  if(!strcmp(h->apikey_where, "query")) {
    if(!query_parse(r->url, &rows)) {
      char *url;
      for(i = rows.n - 1; i >= 0; i--)
        if(!strcmp(rows.r[i].key, h->apikey_name))
          kv_remove(&rows, i);
      url = query_build(r->url, &rows);
      if(url) {
        request_set_url(r, url);
        free(url);
      }
      kv_free(&rows);
    }
  }
  else {
    form_hints none;
    form_hints_init(&none);
    form_headers_get(r, &none, &rows);
    for(i = rows.n - 1; i >= 0; i--)
      if(!strcmp(rows.r[i].key, h->apikey_name))
        kv_remove(&rows, i);
    {
      kv_list keep = h->disabled_headers;
      memset(&h->disabled_headers, 0, sizeof(h->disabled_headers));
      form_headers_set(r, h, &rows);
      kv_free(&h->disabled_headers);
      h->disabled_headers = keep;
    }
    kv_free(&rows);
  }
  free(h->apikey_name);
  h->apikey_name = NULL;
  h->apikey_where[0] = '\0';
}

int form_auth_set(request *r, form_hints *h, auth_kind k, const char *a, const char *b, const char *where,
                  char *err, size_t n)
{
  if(form_auth_kind(r, h) == AUTH_OPTIONS) {
    snprintf(err, n, "Authentication is set by options the form cannot show");
    return -1;
  }
  if((k == AUTH_BASIC || k == AUTH_DIGEST) && a && strchr(a, ':')) {
    snprintf(err, n, "A username cannot contain a colon");
    return -1;
  }
  drop(r, "basic");
  drop(r, "digest");
  drop(r, "user");
  drop(r, "oauth2-bearer");
  apikey_clear(r, h);
  h->auth[0] = '\0';
  if(k == AUTH_BASIC || k == AUTH_DIGEST) {
    char *v = join3(a ? a : "", ":", b ? b : "");
    request_add(r, options_find(k == AUTH_BASIC ? "basic" : "digest"));
    if(v) {
      add_value(r, "user", v);
      free(v);
    }
  }
  else if(k == AUTH_BEARER)
    add_value(r, "oauth2-bearer", a ? a : "");
  else if(k == AUTH_APIKEY) {
    const char *name = a ? a : "";
    const char *value = b ? b : "";
    snprintf(h->auth, sizeof(h->auth), "apikey");
    snprintf(h->apikey_where, sizeof(h->apikey_where), "%s", where && !strcmp(where, "query") ? "query" : "header");
    h->apikey_name = copy_text(name);
    /* no value yet is no header yet: a half typed name must not go out */
    if(name[0] && value[0]) {
      if(!strcmp(h->apikey_where, "query")) {
        kv_list rows;
        if(!query_parse(r->url, &rows)) {
          char *url;
          kv_add(&rows, name, value, 1);
          url = query_build(r->url, &rows);
          if(url) {
            request_set_url(r, url);
            free(url);
          }
          kv_free(&rows);
        }
      }
      else {
        char *line = join3(name, ": ", value);
        if(line) {
          add_value(r, "header", line);
          free(line);
        }
      }
    }
  }
  return 0;
}

int form_auth_literal_secret(const request *r)
{
  const sel_option *s = sel_of(r, "oauth2-bearer");
  const char *v = NULL;
  if(s && s->nvalues)
    v = s->values[0];
  else if((s = sel_of(r, "user")) != NULL && s->nvalues) {
    v = strchr(s->values[0], ':');
    if(v)
      v++;
  }
  return v && v[0] && !strstr(v, "{{");
}
