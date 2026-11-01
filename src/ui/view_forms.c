/* Friendlier views over what the request already is. Nothing here has state of its own that
   curl does not get: a table is read from, and written back to, the URL and the options. */
#include "app.h"

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
int form_kv_table(app_t *a, struct nk_rect r, kv_list *l, const char *key_hint, const char *value_hint, int doc_opt)
{
  const palette *p = &a->pal;
  struct nk_command_buffer *cv = nk_window_get_canvas(a->ctx);
  float content = (float)l->n * KV_ROW + 34;
  float x = r.x, y;
  float key_w = NK_MIN(220, r.w * 0.34f);
  float val_w = r.w - key_w - 22 - 26 - 18;
  int i, changed = 0, remove_row = -1;
  ui_clamp_scroll(&CUR(a)->form_scroll, content, r.h);
  if(!CUR(a)->form_focus)
    ui_wheel(a, r, &CUR(a)->form_scroll, content, 40);
  ui_clamp_scroll(&CUR(a)->form_scroll, content, r.h);
  CUR(a)->form_focus = 0;
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
  if(form_kv_table(a, r, &CUR(a)->params, "key", "value", -1))
    form_params_apply(CUR(a));
}

void view_form(app_t *a, struct nk_rect r)
{
  const palette *p = &a->pal;
  form_params_sync(CUR(a));
  switch(CUR(a)->form) {
  case FORM_PARAMS:
    view_params(a, r);
    break;
  default:
    ui_text_fit(a, a->f.small, r.x, r.y, 24, r.w, "Set these with the options under Curl.", p->dim);
    break;
  }
}
