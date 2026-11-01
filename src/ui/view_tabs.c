#include "app.h"

#include <stdio.h>
#include <string.h>

#define TAB_W 180.0f
#define PLUS_W 30.0f

static void tab_title(const app_t *a, const tab_t *t, char *out, size_t n)
{
  const char *url = t->req.url;
  const char *scheme = strstr(url, "://");
  if(scheme)
    url = scheme + 3;
  snprintf(out, n, "%s %s", app_method_label(a, t, t->method), url[0] ? url : "New request");
}

void view_tabs(app_t *a, struct nk_rect r)
{
  const palette *p = &a->pal;
  struct nk_command_buffer *cv = nk_window_get_canvas(a->ctx);
  float vars_w = a->show_vars ? 100.0f : 0.0f;
  float w = (r.w - PLUS_W - 8 - vars_w) / (float)a->ntabs;
  float x = r.x;
  char title[128];
  int i, close = -1;
  if(w > TAB_W)
    w = TAB_W;
  ui_push_clip(a, r);
  ui_rect(a, r, 0, p->panel);
  nk_stroke_line(cv, r.x, r.y + r.h - 0.5f, r.x + r.w, r.y + r.h - 0.5f, 1.0f, p->border);
  for(i = 0; i < a->ntabs; i++) {
    const tab_t *t = a->tabs[i];
    struct nk_rect box = nk_rect(x, r.y, w, r.h);
    struct nk_rect del = nk_rect(x + w - 22, r.y + (r.h - 18) / 2, 18, 18);
    int current = i == a->cur && !a->show_vars;
    int hover = ui_hover(a, box);
    float tx = x + 10;
    if(current || hover)
      ui_rect(a, nk_rect(box.x, box.y, box.w, box.h - 1), 0, current ? p->card : p->hover);
    if(current)
      nk_fill_rect(cv, nk_rect(box.x, box.y + box.h - 2, box.w, 2), 0, p->accent);
    nk_stroke_line(cv, x + w - 0.5f, r.y + 6, x + w - 0.5f, r.y + r.h - 6, 1.0f, p->border);
    if(t->proc || t->dirty) {
      nk_fill_circle(cv, nk_rect(tx, r.y + r.h / 2 - 3, 6, 6), t->proc ? p->accent : p->amber);
      tx += 10;
    }
    tab_title(a, t, title, sizeof(title));
    ui_text_fit(a, a->f.small, tx, r.y, r.h, x + w - 24 - tx, title, current ? p->text : p->dim);
    if(hover || current) {
      int hd = ui_hover(a, del);
      struct nk_color c = hd ? p->text : p->faint;
      nk_stroke_line(cv, del.x + 5, del.y + 5, del.x + 13, del.y + 13, 1.2f, c);
      nk_stroke_line(cv, del.x + 13, del.y + 5, del.x + 5, del.y + 13, 1.2f, c);
      if(hd)
        a->cursor = CURSOR_HAND;
      if(ui_clicked(a, del))
        close = i;
    }
    if(close != i && hover) {
      a->cursor = CURSOR_HAND;
      if(ui_clicked(a, box) && !current) {
        CUR(a)->reveal = 0;
        a->cur = i;
        a->show_vars = 0;
        a->pending_unfocus = 1;
      }
    }
    x += w;
  }
  if(a->show_vars) {
    ui_rect(a, nk_rect(x, r.y, vars_w, r.h - 1), 0, p->card);
    nk_fill_rect(cv, nk_rect(x, r.y + r.h - 2, vars_w, 2), 0, p->accent);
    ui_text_fit(a, a->f.small, x + 10, r.y, r.h, vars_w - 20, "Variables", p->text);
    x += vars_w;
  }
  if(ui_button(a, nk_rect(x + 4, r.y + 3, PLUS_W - 4, r.h - 6), "+", BTN_GHOST) && tab_new(a) >= 0) {
    a->show_vars = 0;
    a->pending_unfocus = 1;
  }
  if(close >= 0)
    tab_close(a, close);
}
