/* The right side, away from Curl: what this request draws on. */
#include "app.h"

#include <stdio.h>
#include <string.h>

/* Every name the request uses. Walking a large body each frame would show, so it waits for a change. */
void context_panel_names(app_t *a, tab_t *t)
{
  var_env none;
  unsigned long sig = form_request_sig(&t->req);
  if(a->panel_tab == t && a->panel_sig == sig && a->panel_tab)
    return;
  str_list_free(&a->panel_names);
  memset(&none, 0, sizeof(none));
  vars_missing(&none, &t->args_tmpl, &a->panel_names);
  a->panel_tab = t;
  a->panel_sig = sig;
}

static const char *collection_name(const app_t *a, const tab_t *t)
{
  int i;
  for(i = 0; i < a->ncolls; i++)
    if(!strcmp(a->colls[i]->id, tab_coll(t)))
      return a->colls[i]->root->name;
  return "My requests";
}

void view_context(app_t *a, struct nk_rect r)
{
  const palette *p = &a->pal;
  struct nk_command_buffer *cv = nk_window_get_canvas(a->ctx);
  tab_t *t = CUR(a);
  var_store *st = app_tab_store(a, t);
  var_env *env = &st->e[st->active];
  float pad = 18, x = r.x + pad, w = r.w - pad * 2, y = r.y + 12;
  struct nk_rect list;
  float content;
  int i;

  ui_push_clip(a, r);
  ui_rect(a, r, 0, p->panel);
  nk_stroke_line(cv, r.x + 0.5f, r.y, r.x + 0.5f, r.y + r.h, 1.0f, p->border);
  ui_text_fit(a, a->f.title, x, y, 24, w, "This request", p->text);
  y += 32;

  ui_text_fit(a, a->f.small, x, y, 18, 90, "Collection", p->dim);
  ui_text_fit(a, a->f.sans, x + 92, y, 18, w - 92, collection_name(a, t), p->text);
  y += 26;
  ui_text_fit(a, a->f.small, x, y + 5, 18, 90, "Environment", p->dim);
  {
    struct nk_rect btn = nk_rect(x + 92, y, NK_MAX(w - 92, 190), 28);
    if(a->show_vars)  /* the table on the left has its own menu, for its own collection */
      ui_text_fit(a, a->f.sans, btn.x, y, 28, btn.w, env->name, p->text);
    else {
      float cx = btn.x + btn.w - 14, cy = btn.y + 14;
      int hover = ui_hover(a, btn);
      a->env_btn = btn;
      ui_rect(a, btn, 5, hover ? p->hover : p->card);
      ui_stroke(a, btn, 5, a->env_menu ? p->accent : p->border);
      ui_text_fit(a, a->f.sans, btn.x + 9, y, 28, btn.w - 34, env->name, p->text);
      nk_fill_triangle(cv, cx - 4, cy - 2, cx + 4, cy - 2, cx, cy + 3, p->dim);
      if(hover)
        a->cursor = CURSOR_HAND;
      if(ui_clicked(a, btn)) {
        snprintf(a->vars_for, sizeof(a->vars_for), "%s", tab_coll(t));
        a->env_menu = !a->env_menu;
      }
    }
  }
  y += 38;

  ui_text_fit(a, a->f.semi, x, y, 26, w - 124, "Variables it uses", p->text);
  if(ui_button(a, nk_rect(x + w - 118, y, 118, 26), "Open Variables", BTN_SECONDARY))
    app_open_vars(a, tab_coll(t));
  y += 34;

  context_panel_names(a, t);
  if(!a->panel_names.n) {
    ui_text_fit(a, a->f.small, x, y, 18, w, "None yet. Write {{name}} in the URL, a header or the body.", p->dim);
    return;
  }
  list = nk_rect(r.x, y, r.w, r.y + r.h - y);
  content = (float)a->panel_names.n * 44;
  ui_clamp_scroll(&a->panel_scroll, content, list.h);
  ui_wheel(a, list, &a->panel_scroll, content, 40);
  ui_push_clip(a, list);
  y -= a->panel_scroll;
  for(i = 0; i < a->panel_names.n; i++) {
    const var_t *v = var_find(env, a->panel_names.s[i]);
    if(y + 44 >= list.y && y <= list.y + list.h) {
      ui_text_fit(a, a->f.mono_small, x, y, 18, w, a->panel_names.s[i], p->accent);
      if(!v)
        ui_text_fit(a, a->f.small, x, y + 18, 18, w, "missing: Run will ask for it", p->amber);
      else if(v->secret)
        ui_text_fit(a, a->f.mono_small, x, y + 18, 18, w, "********", p->dim);
      else
        ui_text_fit(a, a->f.mono_small, x, y + 18, 18, w, v->value[0] ? v->value : "(empty)", v->value[0] ? p->text : p->faint);
    }
    y += 44;
  }
  ui_scrollbar(a, list, &a->panel_scroll, content);
}
