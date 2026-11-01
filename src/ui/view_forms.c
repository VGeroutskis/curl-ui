/* Friendlier views over what the request already is. Nothing here has state of its own that
   curl does not get: a table is read from, and written back to, the URL and the options. */
#include "app.h"
#include "forms.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KV_ROW 36.0f

static unsigned long row_hash(const kv_row *r)
{
  unsigned long h = 2166136261u + (unsigned long)r->enabled;
  const char *s;
  for(s = r->key; *s; s++)
    h = (h ^ (unsigned char)*s) * 16777619u;
  h = (h ^ 0xFF) * 16777619u;
  for(s = r->value; *s; s++)
    h = (h ^ (unsigned char)*s) * 16777619u;
  return h;
}

/* A table of rows with a switch, key, value and a remove button. Returns 1 when something changed. */
int form_kv_table(app_t *a, struct nk_rect r, kv_list *l, const char *key_hint, const char *value_hint, int doc_opt,
                  int file_col)
{
  const palette *p = &a->pal;
  struct nk_command_buffer *cv = nk_window_get_canvas(a->ctx);
  float content = (float)l->n * KV_ROW + 34;
  float x = r.x, y;
  float key_w = NK_MIN(220, r.w * 0.34f);
  float file_w = file_col ? 52.0f : 0.0f;
  float val_w = r.w - key_w - 22 - 26 - 18 - file_w;
  int i, changed = 0, remove_row = -1;
  ui_clamp_scroll(&CUR(a)->form_scroll, content, r.h);
  if(!CUR(a)->form_focus_prev)
    ui_wheel(a, r, &CUR(a)->form_scroll, content, 40);
  ui_clamp_scroll(&CUR(a)->form_scroll, content, r.h);
  ui_push_clip(a, r);
  y = r.y - CUR(a)->form_scroll;
  for(i = 0; i < l->n; i++) {
    kv_row *row = &l->r[i];
    unsigned long before = row_hash(row);
    struct nk_rect box = nk_rect(x + 2, y + 7, 16, 16);
    struct nk_rect del = nk_rect(x + r.w - 26, y + 6, 18, 18);
    float fx = x + 26;
    nk_flags st;
    if(doc_opt >= 0 && ui_hover(a, nk_rect(x, y, r.w, KV_ROW)))
      ui_hover_doc(a, doc_opt);
    ui_rect(a, box, 3, row->enabled ? p->accent : p->field);
    ui_stroke(a, box, 3, row->enabled ? p->accent : p->border_strong);
    if(ui_hover(a, box))
      a->cursor = CURSOR_HAND;
    if(ui_clicked(a, box))
      row->enabled = !row->enabled;
    st = ui_edit_dyn(a, nk_rect(fx, y, key_w, 30), &row->key, &row->key_cap, key_hint, a->f.mono_small, 0);
    if(st & NK_EDIT_ACTIVE)
      CUR(a)->form_focus = 1;
    fx += key_w + 6;
    st = ui_edit_dyn(a, nk_rect(fx, y, val_w, 30), &row->value, &row->value_cap, value_hint, a->f.mono_small, 0);
    if(st & NK_EDIT_ACTIVE)
      CUR(a)->form_focus = 1;
    if(file_col) {
      struct nk_rect fb = nk_rect(fx + val_w + 6, y + 7, 16, 16);
      struct nk_rect ft = nk_rect(fb.x, y, file_w, 30);
      ui_rect(a, fb, 3, row->is_file ? p->accent : p->field);
      ui_stroke(a, fb, 3, row->is_file ? p->accent : p->border_strong);
      ui_text_fit(a, a->f.small, fb.x + 20, y, 30, 26, "File", p->dim);
      if(ui_hover(a, ft))
        a->cursor = CURSOR_HAND;
      if(ui_clicked(a, ft)) {
        row->is_file = !row->is_file;
        changed = 1;
      }
    }
    {
      int hd = ui_hover(a, del);
      struct nk_color c = hd ? p->text : p->faint;
      nk_stroke_line(cv, del.x + 5, del.y + 5, del.x + 13, del.y + 13, 1.2f, c);
      nk_stroke_line(cv, del.x + 13, del.y + 5, del.x + 5, del.y + 13, 1.2f, c);
      if(hd)
        a->cursor = CURSOR_HAND;
      if(ui_clicked(a, del))
        remove_row = i;
    }
    if(row_hash(row) != before)
      changed = 1;
    y += KV_ROW;
  }
  if(remove_row >= 0) {
    kv_remove(l, remove_row);
    a->pending_unfocus = 1;
    changed = 1;
  }
  if(ui_button(a, nk_rect(x, y + 2, 80, 24), "+ Add row", BTN_GHOST)) {
    kv_add(l, "", "", 1);
    changed = 1;
  }
  ui_scrollbar(a, r, &CUR(a)->form_scroll, content);
  return changed;
}

/* The table follows the URL: rows come from it, except those switched off, which only the
   table knows about. */
void form_params_sync(tab_t *t)
{
  kv_list fresh;
  size_t n;
  int i;
  if(t->params_url && !strcmp(t->params_url, t->req.url))
    return;
  if(query_parse(t->req.url, &fresh))
    return;
  for(i = 0; i < t->params.n; i++)
    if(!t->params.r[i].enabled)
      kv_add(&fresh, t->params.r[i].key, t->params.r[i].value, 0);
  kv_free(&t->params);
  t->params = fresh;
  free(t->params_url);
  n = strlen(t->req.url) + 1;
  t->params_url = malloc(n);
  if(t->params_url)
    memcpy(t->params_url, t->req.url, n);
}

/* And the URL follows the table when the table is what changed. */
void form_params_apply(tab_t *t)
{
  char *url = query_build(t->req.url, &t->params);
  size_t n;
  if(!url)
    return;
  request_set_url(&t->req, url);
  free(t->params_url);
  n = strlen(url) + 1;
  t->params_url = malloc(n);
  if(t->params_url)
    memcpy(t->params_url, url, n);
  free(url);
}

static void view_params(app_t *a, struct nk_rect r)
{
  if(form_kv_table(a, r, &CUR(a)->params, "key", "value", -1, 0))
    form_params_apply(CUR(a));
}

/* The forms' own rows are read again whenever the request changed some other way. */
unsigned long form_request_sig(const request *q)
{
  unsigned long h = 2166136261u;
  const char *s;
  int i, j;
  for(s = q->url; *s; s++)
    h = (h ^ (unsigned char)*s) * 16777619u;
  for(i = 0; i < q->nsel; i++) {
    h = (h ^ (unsigned long)q->sel[i].opt) * 16777619u;
    for(j = 0; j < q->sel[i].nvalues; j++) {
      for(s = q->sel[i].values[j]; *s; s++)
        h = (h ^ (unsigned char)*s) * 16777619u;
      h = (h ^ 0xFF) * 16777619u;
    }
  }
  return h;
}

static void set_text(char **buf, size_t *cap, const char *text)
{
  size_t n = strlen(text) + 1;
  if(n > *cap) {
    char *g = realloc(*buf, n);
    if(!g)
      return;
    *buf = g;
    *cap = n;
  }
  memcpy(*buf, text, n);
}

static void forms_read(tab_t *t)
{
  char *text = NULL, *x = NULL, *y = NULL;
  kv_list rows;
  kv_free(&t->header_rows);
  form_headers_get(&t->req, &t->hints, &t->header_rows);
  if(!form_body_get(&t->req, &rows, &text)) {
    /* rows switched off live only here */
    int i;
    for(i = 0; i < t->body_rows.n; i++) {
      if(!t->body_rows.r[i].enabled) {
        kv_row *off = kv_add(&rows, t->body_rows.r[i].key, t->body_rows.r[i].value, 0);
        if(off)
          off->is_file = t->body_rows.r[i].is_file;
      }
    }
    kv_free(&t->body_rows);
    t->body_rows = rows;
    if(text) {
      set_text(&t->body_file, &t->body_file_cap, text);
      free(text);
    }
  }
  if(!form_auth_get(&t->req, &t->hints, &x, &y)) {
    set_text(&t->auth_a, &t->auth_a_cap, x ? x : "");
    set_text(&t->auth_b, &t->auth_b_cap, y ? y : "");
  }
  free(x);
  free(y);
  if(t->hints.apikey_where[0])
    t->auth_query = !strcmp(t->hints.apikey_where, "query");
}

static int segment_row(app_t *a, float x, float y, float w, const char *const *labels, int n, int sel)
{
  const palette *p = &a->pal;
  float bw = w / (float)n;
  int i, clicked = -1;
  ui_rect(a, nk_rect(x, y, w, 26), 5, p->bg);
  for(i = 0; i < n; i++) {
    struct nk_rect c = nk_rect(x + (float)i * bw, y, bw, 26);
    int hover = ui_hover(a, c);
    float tw = ui_text_w(a->f.small, labels[i], (int)strlen(labels[i]));
    if(i == sel)
      ui_rect(a, c, 5, p->accent);
    else if(hover)
      ui_rect(a, c, 5, p->hover);
    ui_text(a, a->f.small, c.x + (c.w - tw) / 2, c.y + (c.h - a->f.small->height) / 2, labels[i],
            (int)strlen(labels[i]), i == sel ? p->on_accent : p->dim);
    if(hover)
      a->cursor = CURSOR_HAND;
    if(ui_clicked(a, c))
      clicked = i;
  }
  ui_stroke(a, nk_rect(x, y, w, 26), 5, p->border);
  return clicked;
}

static void set_by_options(app_t *a, struct nk_rect r, const char *what)
{
  const palette *p = &a->pal;
  char tmp[128];
  snprintf(tmp, sizeof(tmp), "Set by options: %s", what);
  ui_text_fit(a, a->f.sans, r.x, r.y + 2, 24, r.w, tmp, p->amber);
  ui_text_fit(a, a->f.small, r.x, r.y + 28, 18, r.w, "This form cannot show them all, so it leaves them as they are.", p->dim);
  if(ui_button(a, nk_rect(r.x, r.y + 54, 110, 26), "Open Curl", BTN_SECONDARY)) {
    CUR(a)->form = FORM_CURL;
    a->pending_unfocus = 1;
  }
}

static void view_headers(app_t *a, struct nk_rect r)
{
  tab_t *t = CUR(a);
  if(form_kv_table(a, r, &t->header_rows, "name", "value", options_find("header"), 0)) {
    form_headers_set(&t->req, &t->hints, &t->header_rows);
    t->form_sig = form_request_sig(&t->req);
  }
}

static void view_body(app_t *a, struct nk_rect r)
{
  static const char *const names[] = { "None", "Raw", "JSON", "Form data", "URL encoded", "File" };
  static const body_kind kinds[] = { BODY_NONE, BODY_RAW, BODY_JSON, BODY_FORMDATA, BODY_URLENC, BODY_FILE };
  const palette *p = &a->pal;
  tab_t *t = CUR(a);
  body_kind k = form_body_kind(&t->req);
  char err[128];
  int i, sel = 0, clicked;
  struct nk_rect area;
  if(!t->body_file)
    set_text(&t->body_file, &t->body_file_cap, "");
  if(k == BODY_OPTIONS) {
    set_by_options(a, r, "--data, --form and others together");
    return;
  }
  /* an empty table says nothing in the request: the choice is remembered here */
  if(k == BODY_NONE && (t->body_choice == BODY_FORMDATA || t->body_choice == BODY_URLENC))
    k = t->body_choice;
  for(i = 0; i < 6; i++)
    if(kinds[i] == k)
      sel = i;
  clicked = segment_row(a, r.x, r.y, NK_MIN(r.w, 460), names, 6, sel);
  if(clicked >= 0 && kinds[clicked] != k) {
    body_kind to = kinds[clicked];
    char *keep = NULL;
    if((k == BODY_RAW && to == BODY_JSON) || (k == BODY_JSON && to == BODY_RAW)) {
      const char *cur = t->req.sel[request_find(&t->req, options_find(k == BODY_RAW ? "data-raw" : "json"))].values[0];
      size_t n = strlen(cur) + 1;
      keep = malloc(n);
      if(keep)
        memcpy(keep, cur, n);
    }
    t->body_choice = to;
    form_body_set(&t->req, to, &t->body_rows, keep ? keep : "", err, sizeof(err));
    free(keep);
    k = to;
    a->pending_unfocus = 1;
    t->form_sig = form_request_sig(&t->req);
  }
  area = nk_rect(r.x, r.y + 34, r.w, r.h - 34);
  if(k == BODY_NONE)
    ui_text_fit(a, a->f.small, area.x, area.y, 22, area.w, "No body. A method such as POST can still be chosen above.", p->dim);
  else if(k == BODY_RAW || k == BODY_JSON) {
    /* the option's own value, edited in place */
    int slot = request_find(&t->req, options_find(k == BODY_RAW ? "data-raw" : "json"));
    sel_option *s = slot >= 0 ? &t->req.sel[slot] : NULL;
    if(s) {
      nk_flags st = ui_edit_dyn(a, area, &s->values[0], &s->caps[0], k == BODY_RAW ? "body text" : "{ }", a->f.mono_small,
                                NK_EDIT_MULTILINE);
      if(st & NK_EDIT_ACTIVE)
        t->form_focus = 1;
      if(ui_hover(a, area))
        ui_hover_doc(a, s->opt);
    }
  }
  else if(k == BODY_FILE) {
    nk_flags st;
    unsigned long before = 0;
    const char *c;
    for(c = t->body_file; c && *c; c++)
      before = (before ^ (unsigned char)*c) * 16777619u;
    ui_text_fit(a, a->f.small, area.x, area.y, 18, area.w, "Full path of the file to send, as it is", p->dim);
    st = ui_edit_dyn(a, nk_rect(area.x, area.y + 22, area.w, 30), &t->body_file, &t->body_file_cap, "/full/path/to/file",
                     a->f.mono_small, 0);
    if(st & NK_EDIT_ACTIVE)
      t->form_focus = 1;
    {
      unsigned long after = 0;
      for(c = t->body_file; *c; c++)
        after = (after ^ (unsigned char)*c) * 16777619u;
      if(after != before) {
        form_body_set(&t->req, BODY_FILE, NULL, t->body_file, err, sizeof(err));
        t->form_sig = form_request_sig(&t->req);
      }
    }
  }
  else {
    int opt = options_find(k == BODY_URLENC ? "data-urlencode" : "form");
    if(form_kv_table(a, area, &t->body_rows, "name", k == BODY_FORMDATA ? "value or path" : "value", opt, k == BODY_FORMDATA)) {
      t->body_choice = k;
      form_body_set(&t->req, k, &t->body_rows, NULL, err, sizeof(err));
      t->form_sig = form_request_sig(&t->req);
    }
  }
}

static int edit_row(app_t *a, float x, float y, float w, const char *label, char **buf, size_t *cap, const char *hint)
{
  const palette *p = &a->pal;
  unsigned long before = 2166136261u, after = 2166136261u;
  const char *c;
  nk_flags st;
  for(c = *buf; c && *c; c++)
    before = (before ^ (unsigned char)*c) * 16777619u;
  ui_text_fit(a, a->f.small, x, y, 30, 90, label, p->dim);
  st = ui_edit_dyn(a, nk_rect(x + 94, y, w - 94, 30), buf, cap, hint, a->f.mono_small, 0);
  if(st & NK_EDIT_ACTIVE)
    CUR(a)->form_focus = 1;
  for(c = *buf; *c; c++)
    after = (after ^ (unsigned char)*c) * 16777619u;
  return after != before;
}

static void view_auth(app_t *a, struct nk_rect r)
{
  static const char *const names[] = { "None", "Basic", "Bearer", "Digest", "API key" };
  static const auth_kind kinds[] = { AUTH_NONE, AUTH_BASIC, AUTH_BEARER, AUTH_DIGEST, AUTH_APIKEY };
  const palette *p = &a->pal;
  tab_t *t = CUR(a);
  auth_kind k = form_auth_kind(&t->req, &t->hints);
  int i, sel = 0, clicked, changed = 0;
  float y;
  if(k == AUTH_OPTIONS) {
    set_by_options(a, r, "authentication options the form does not offer");
    return;
  }
  if(!t->auth_a) {
    set_text(&t->auth_a, &t->auth_a_cap, "");
    set_text(&t->auth_b, &t->auth_b_cap, "");
  }
  for(i = 0; i < 5; i++)
    if(kinds[i] == k)
      sel = i;
  clicked = segment_row(a, r.x, r.y, NK_MIN(r.w, 400), names, 5, sel);
  if(clicked >= 0 && kinds[clicked] != k) {
    k = kinds[clicked];
    t->auth_a[0] = t->auth_b[0] = '\0';
    changed = 1;
    a->pending_unfocus = 1;
  }
  y = r.y + 36;
  if(k == AUTH_BASIC || k == AUTH_DIGEST) {
    changed |= edit_row(a, r.x, y, r.w, "Username", &t->auth_a, &t->auth_a_cap, "user");
    changed |= edit_row(a, r.x, y + 36, r.w, "Password", &t->auth_b, &t->auth_b_cap, "{{password}}");
    y += 72;
  }
  else if(k == AUTH_BEARER) {
    changed |= edit_row(a, r.x, y, r.w, "Token", &t->auth_a, &t->auth_a_cap, "{{token}}");
    y += 36;
  }
  else if(k == AUTH_APIKEY) {
    static const char *const where[] = { "Header", "Query" };
    changed |= edit_row(a, r.x, y, r.w, "Name", &t->auth_a, &t->auth_a_cap, "X-Api-Key");
    changed |= edit_row(a, r.x, y + 36, r.w, "Value", &t->auth_b, &t->auth_b_cap, "{{api_key}}");
    ui_text_fit(a, a->f.small, r.x, y + 72, 26, 90, "Send in", p->dim);
    clicked = segment_row(a, r.x + 94, y + 72, 160, where, 2, t->auth_query);
    if(clicked >= 0 && clicked != t->auth_query) {
      t->auth_query = clicked;
      changed = 1;
    }
    y += 104;
  }
  if(changed) {
    t->auth_error[0] = '\0';
    form_auth_set(&t->req, &t->hints, k, t->auth_a, t->auth_b, t->auth_query ? "query" : "header", t->auth_error,
                  sizeof(t->auth_error));
    /* a key may have gone into or out of the headers: that table must not write back old rows */
    kv_free(&t->header_rows);
    form_headers_get(&t->req, &t->hints, &t->header_rows);
    t->form_sig = form_request_sig(&t->req);
  }
  if(t->auth_error[0]) {
    ui_text_fit(a, a->f.small, r.x, y, 18, r.w, t->auth_error, p->red);
    y += 20;
  }
  if(k != AUTH_NONE && strncmp(t->req.url, "https://", 8) && strncmp(t->req.url, "{{", 2)) {
    ui_text_fit(a, a->f.small, r.x, y, 18, r.w, "This URL is not https://: these credentials travel readable.", p->amber);
    y += 20;
  }
  if(form_auth_literal_secret(&t->req))
    ui_text_fit(a, a->f.small, r.x, y, 18, r.w, "Written here, this goes into history and collections. Use a {{variable}} to keep it out.", p->amber);
}

void view_form(app_t *a, struct nk_rect r)
{
  tab_t *t = CUR(a);
  unsigned long sig = form_request_sig(&t->req);
  form_params_sync(t);
  t->form_focus_prev = t->form_focus;
  t->form_focus = 0;
  if(sig != t->form_sig && !t->form_focus_prev) {
    forms_read(t);
    t->form_sig = sig;
  }
  ui_push_clip(a, r);
  switch(t->form) {
  case FORM_PARAMS:
    view_params(a, r);
    break;
  case FORM_HEADERS:
    view_headers(a, r);
    break;
  case FORM_BODY:
    view_body(a, r);
    break;
  case FORM_AUTH:
    view_auth(a, r);
    break;
  default:
    break;
  }
}
