#include "reqjson.h"
#include "options.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static jv *kv_to_jv(const kv_list *l)
{
  jv *arr = jv_new(JV_ARR);
  int i;
  for(i = 0; i < l->n; i++) {
    jv *row = jv_new(JV_OBJ);
    jv_set(row, "key", jv_new_str(l->r[i].key));
    jv_set(row, "value", jv_new_str(l->r[i].value));
    if(l->r[i].is_file)
      jv_set(row, "file", jv_new_bool(1));
    jv_push(arr, row);
  }
  return arr;
}

static void kv_from_jv(const jv *arr, kv_list *l)
{
  int i;
  for(i = 0; i < jv_len(arr); i++) {
    const char *k = jv_str(jv_get(jv_at(arr, i), "key"));
    const char *v = jv_str(jv_get(jv_at(arr, i), "value"));
    if(k && v) {
      kv_row *row = kv_add(l, k, v, 0);
      if(row)
        row->is_file = jv_bool(jv_get(jv_at(arr, i), "file"));
    }
  }
}

jv *req_to_jv(const request *r, const char *method, const form_hints *h, const jv *extra)
{
  jv *o = jv_new(JV_OBJ), *opts = jv_new(JV_ARR), *hints = jv_new(JV_OBJ);
  int i, j;
  jv_set(o, "method", jv_new_str(method ? method : ""));
  jv_set(o, "url", jv_new_str(r->url));
  for(i = 0; i < r->nsel; i++) {
    const sel_option *s = &r->sel[i];
    jv *row = jv_new(JV_OBJ), *vals = jv_new(JV_ARR);
    jv_set(row, "name", jv_new_str(g_options[s->opt].name));
    if(s->negated)
      jv_set(row, "negated", jv_new_bool(1));
    for(j = 0; j < s->nvalues; j++)
      jv_push(vals, jv_new_str(s->values[j]));
    jv_set(row, "values", vals);
    jv_push(opts, row);
  }
  for(i = 0; i < jv_len(extra); i++)
    jv_push(opts, jv_clone(jv_at(extra, i)));
  jv_set(o, "options", opts);
  if(h) {
    jv_set(hints, "auth", jv_new_str(h->auth));
    if(h->apikey_name) {
      jv_set(hints, "apikey_name", jv_new_str(h->apikey_name));
      jv_set(hints, "apikey_where", jv_new_str(h->apikey_where));
    }
    jv_set(hints, "disabled_params", kv_to_jv(&h->disabled_params));
    jv_set(hints, "disabled_headers", kv_to_jv(&h->disabled_headers));
    jv_set(hints, "disabled_body", kv_to_jv(&h->disabled_body));
  }
  jv_set(o, "form_hints", hints);
  return o;
}

int req_from_jv(const jv *v, request *r, char *method, size_t methodn, form_hints *h, jv **extra)
{
  const jv *opts = jv_get(v, "options"), *hints = jv_get(v, "form_hints");
  const char *s;
  int i, j;
  *extra = NULL;
  if(jv_kind(v) != JV_OBJ)
    return -1;
  s = jv_str(jv_get(v, "method"));
  snprintf(method, methodn, "%s", s ? s : "");
  s = jv_str(jv_get(v, "url"));
  if(s)
    request_set_url(r, s);
  for(i = 0; i < jv_len(opts); i++) {
    const jv *row = jv_at(opts, i), *vals = jv_get(row, "values");
    const char *name = jv_str(jv_get(row, "name"));
    int opt = name ? options_find(name) : -1;
    sel_option *sel;
    if(opt < 0) {
      if(!*extra)
        *extra = jv_new(JV_ARR);
      jv_push(*extra, jv_clone(row));
      continue;
    }
    sel = request_add(r, opt);
    if(!sel)
      continue;
    sel->negated = jv_bool(jv_get(row, "negated"));
    for(j = 0; j < jv_len(vals); j++) {
      const char *val = jv_str(jv_at(vals, j));
      if(!val || !g_options[opt].arg[0])
        continue;
      if(j == 0 && sel->nvalues)
        request_set_value(sel, 0, val);
      else
        request_append_value(sel, val);
    }
  }
  s = jv_str(jv_get(hints, "auth"));
  if(s)
    snprintf(h->auth, sizeof(h->auth), "%s", s);
  s = jv_str(jv_get(hints, "apikey_name"));
  if(s) {
    size_t n = strlen(s) + 1;
    h->apikey_name = malloc(n);
    if(h->apikey_name)
      memcpy(h->apikey_name, s, n);
  }
  s = jv_str(jv_get(hints, "apikey_where"));
  if(s)
    snprintf(h->apikey_where, sizeof(h->apikey_where), "%s", s);
  kv_from_jv(jv_get(hints, "disabled_params"), &h->disabled_params);
  kv_from_jv(jv_get(hints, "disabled_headers"), &h->disabled_headers);
  kv_from_jv(jv_get(hints, "disabled_body"), &h->disabled_body);
  return 0;
}
