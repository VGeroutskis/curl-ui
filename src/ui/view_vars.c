/* The table of variables: the one place their values come from. */
#include "app.h"

#include <stdio.h>
#include <string.h>

#define ROW 38.0f

static unsigned long hash_text(unsigned long h, const char *s)
{
  for(; *s; s++)
    h = (h ^ (unsigned char)*s) * 16777619u;
  return (h ^ 0xFF) * 16777619u;
}

static unsigned long store_hash(const var_store *s)
{
  unsigned long h = 2166136261u + (unsigned long)s->active;
  int i, k;
  for(i = 0; i < s->n; i++) {
    h = hash_text(h, s->e[i].name);
    for(k = 0; k < s->e[i].n; k++) {
      h = hash_text(h, s->e[i].v[k].name);
      h = hash_text(h, s->e[i].v[k].value);
      h = (h ^ (unsigned long)s->e[i].v[k].secret) * 16777619u;
    }
  }
  return h;
}

unsigned long app_vars_hash(const app_t *a)
{
  return store_hash(&a->vars);
}

void app_vars_save(app_t *a)
{
  a->vars_hash = store_hash(&a->vars);
  if(a->vars_path[0] && vars_save(&a->vars, a->vars_path))
    app_notice(a, "Could not save the variables. They are still here until you close the app.");
}

void app_vars_autosave(app_t *a)
{
  if(!a->any_focus && store_hash(&a->vars) != a->vars_hash)
    app_vars_save(a);
}

void app_vars_flush(app_t *a)
{
  if(store_hash(&a->vars) != a->vars_hash)
    app_vars_save(a);
}

static void hide_values(var_env *e)
{
  int i;
  for(i = 0; i < e->n; i++)
    e->v[i].reveal = 0;
}

static void stars(app_t *a, struct nk_rect r)
{
  const palette *p = &a->pal;
  ui_rect(a, r, 5, p->field);
  ui_stroke(a, r, 5, p->border);
  ui_text_fit(a, a->f.mono_small, r.x + 9, r.y, r.h, r.w - 16, "********", p->dim);
}

static void env_controls(app_t *a, float x, float y, float w)
{
  const palette *p = &a->pal;
  var_store *st = &a->vars;
  float name_w = w - 196 - 62;
  nk_flags state;
  int taken;
  if(a->env_edit_for != st->active || !a->env_edit[0]) {
    if(!(a->env_edit_for == st->active && a->env_editing)) {
      snprintf(a->env_edit, sizeof(a->env_edit), "%s", st->e[st->active].name);
      a->env_edit_for = st->active;
    }
  }
  ui_text_fit(a, a->f.small, x, y, 30, 56, "Name", p->dim);
  state = ui_edit(a, nk_rect(x + 40, y, name_w - 40, 30), a->env_edit, (int)sizeof(a->env_edit), "name", a->f.sans, 0);
  a->env_editing = (state & NK_EDIT_ACTIVE) != 0;
  taken = strcmp(a->env_edit, st->e[st->active].name) != 0;
  if(taken && env_rename(st, st->active, a->env_edit))
    ui_stroke(a, nk_rect(x + 40, y, name_w - 40, 30), 5, p->amber);
  x += name_w + 8;
  if(ui_button(a, nk_rect(x, y + 1, 56, 28), "New", BTN_SECONDARY)) {
    char name[64];
    int i, to = -1;
    for(i = 1; i < 1000 && to < 0; i++) {
      snprintf(name, sizeof(name), "Environment %d", i);
      to = env_add(st, name);
    }
    if(to >= 0)
      st->active = to;
    a->pending_unfocus = 1;
  }
  x += 60;
  if(ui_button(a, nk_rect(x, y + 1, 66, 28), "Duplicate", BTN_SECONDARY)) {
    char name[160];
    int i, to = -1;
    for(i = 1; i < 1000 && to < 0; i++) {
      snprintf(name, sizeof(name), "%.120s copy%s%.0d", st->e[st->active].name, i > 1 ? " " : "", i > 1 ? i : 0);
      to = env_copy(st, st->active, name);
    }
    if(to >= 0)
      st->active = to;
    a->pending_unfocus = 1;
  }
  x += 70;
  if(st->n > 1 && ui_button(a, nk_rect(x, y + 1, 62, 28), a->env_del_armed ? "Really?" : "Delete",
                            a->env_del_armed ? BTN_DANGER : BTN_SECONDARY)) {
    if(a->env_del_armed) {
      env_remove(st, st->active);
      a->env_del_armed = 0;
      /* another one may now be at this index: it keeps its own name */
      a->env_edit_for = -1;
      a->env_editing = 0;
      a->pending_unfocus = 1;
    }
    else
      a->env_del_armed = 1;
  }
}

void view_vars(app_t *a, struct nk_rect r)
{
  const palette *p = &a->pal;
  var_env *e = &a->vars.e[a->vars.active];
  struct nk_rect body;
  float x = r.x + 16, w = r.w - 32 - 8, y;
  float name_w = NK_MIN(190, w * 0.32f), sec_w = 60, eye_w = 28, del_w = 24;
  float val_w = w - name_w - sec_w - eye_w - del_w - 24;
  float content;
  int i, remove_row = -1;

  ui_push_clip(a, r);
  ui_text_fit(a, a->f.title, x, r.y + 12, 28, 200, "Variables", p->text);
  if(ui_button(a, nk_rect(r.x + r.w - 16 - 70, r.y + 12, 70, 28), "Done", BTN_PRIMARY)) {
    hide_values(e);
    a->show_vars = 0;
    a->pending_unfocus = 1;
  }
  env_controls(a, x, r.y + 48, w);
  e = &a->vars.e[a->vars.active];
  ui_text_fit(a, a->f.small, x, r.y + 84, 18, w, "Used as {{name}} in the URL and in any option. Only this table gives them values.", p->faint);

  y = r.y + 108;
  ui_text_fit(a, a->f.small, x, y, 20, name_w, "Name", p->dim);
  ui_text_fit(a, a->f.small, x + name_w + 8, y, 20, val_w, "Value", p->dim);
  ui_text_fit(a, a->f.small, x + name_w + val_w + 16, y, 20, sec_w, "Secret", p->dim);
  body = nk_rect(r.x, y + 24, r.w, r.h - (y + 24 - r.y));
  content = (float)e->n * ROW + 44;
  ui_clamp_scroll(&a->vars_scroll, content, body.h);
  if(!a->vars_focus)
    ui_wheel(a, body, &a->vars_scroll, content, 50);
  ui_clamp_scroll(&a->vars_scroll, content, body.h);
  a->vars_focus = 0;
  ui_push_clip(a, body);
  y = body.y - a->vars_scroll;

  for(i = 0; i < e->n; i++) {
    var_t *v = &e->v[i];
    float fx = x;
    int bad = !vars_name_ok(v->name) || var_find(e, v->name) != v;
    int hidden = v->secret && !v->reveal;
    struct nk_rect value_r;
    struct nk_rect box, target, eye, del;
    nk_flags st = ui_edit_dyn(a, nk_rect(fx, y, name_w, 30), &v->name, &v->name_cap, "name", a->f.mono_small, 0);
    if(st & NK_EDIT_ACTIVE)
      a->vars_focus = 1;
    if(bad && v->name[0])
      ui_stroke(a, nk_rect(fx, y, name_w, 30), 5, p->amber);
    fx += name_w + 8;
    value_r = nk_rect(fx, y, val_w, 30);
    if(hidden)
      stars(a, value_r);
    else {
      st = ui_edit_dyn(a, value_r, &v->value, &v->value_cap, "value", a->f.mono_small, 0);
      if(st & NK_EDIT_ACTIVE)
        a->vars_focus = 1;
    }
    if(v->secret && v->value[0] && strlen(v->value) < MASK_MIN_LEN)
      ui_text_fit(a, a->f.small, value_r.x + value_r.w - 150, y, 30, 144, "too short to hide in output", p->amber);
    fx += val_w + 8;
    box = nk_rect(fx + 6, y + 7, 16, 16);
    target = nk_rect(fx, y, sec_w, 30);
    ui_rect(a, box, 3, v->secret ? p->accent : p->field);
    ui_stroke(a, box, 3, v->secret ? p->accent : p->border_strong);
    if(ui_hover(a, target))
      a->cursor = CURSOR_HAND;
    if(ui_clicked(a, target)) {
      v->secret = !v->secret;
      v->reveal = 0;
      a->pending_unfocus = 1;
    }
    fx += sec_w;
    eye = nk_rect(fx, y + 4, eye_w - 4, 22);
    if(v->secret && ui_eye(a, eye, v->reveal)) {
      v->reveal = !v->reveal;
      a->pending_unfocus = 1;
    }
    fx += eye_w;
    del = nk_rect(fx, y + 6, 18, 18);
    {
      int hd = ui_hover(a, del);
      struct nk_color c = hd ? p->text : p->faint;
      struct nk_command_buffer *cv = nk_window_get_canvas(a->ctx);
      nk_stroke_line(cv, del.x + 5, del.y + 5, del.x + 13, del.y + 13, 1.2f, c);
      nk_stroke_line(cv, del.x + 13, del.y + 5, del.x + 5, del.y + 13, 1.2f, c);
      if(hd)
        a->cursor = CURSOR_HAND;
      if(ui_clicked(a, del))
        remove_row = i;
    }
    y += ROW;
  }
  if(remove_row >= 0) {
    var_remove(e, remove_row);
    a->pending_unfocus = 1;
  }
  if(ui_button(a, nk_rect(x, y + 4, 110, 26), "+ Add variable", BTN_GHOST) && !var_find(e, ""))
    var_set(e, "", "", 0);
  ui_scrollbar(a, body, &a->vars_scroll, content);
  app_vars_autosave(a);
}

/* ---- the environment menu in the header ---- */

struct nk_rect env_menu_rect(app_t *a)
{
  float h = (float)(a->vars.n + 1) * 26 + 8;
  return nk_rect(a->env_btn.x, a->env_btn.y + a->env_btn.h + 4, NK_MAX(a->env_btn.w, 190), h);
}

int env_menu_item_at(app_t *a, struct nk_vec2 pos)
{
  struct nk_rect m = env_menu_rect(a);
  int i;
  for(i = 0; i <= a->vars.n; i++)
    if(NK_INBOX(pos.x, pos.y, m.x + 4, m.y + 4 + (float)i * 26, m.w - 8, 26))
      return i;
  return -1;
}

void env_menu_pick(app_t *a, int item)
{
  if(item < 0)
    return;
  if(item < a->vars.n) {
    a->vars.active = item;
    a->env_del_armed = 0;
    app_vars_save(a);
  }
  else {
    a->show_vars = 1;
    a->show_settings = 0;
  }
  a->pending_unfocus = 1;
}

void env_menu_draw(app_t *a)
{
  const palette *p = &a->pal;
  struct nk_rect m = env_menu_rect(a);
  const struct nk_input *in = &a->ctx->input;
  int i;
  ui_push_clip(a, nk_rect(0, 0, a->W, a->H));
  ui_rect(a, nk_rect(m.x + 1, m.y + 2, m.w, m.h), 7, nk_rgba(0, 0, 0, 60));
  ui_rect(a, m, 7, p->card);
  ui_stroke(a, m, 7, p->border_strong);
  for(i = 0; i <= a->vars.n; i++) {
    struct nk_rect item = nk_rect(m.x + 4, m.y + 4 + (float)i * 26, m.w - 8, 26);
    if(nk_input_is_mouse_hovering_rect(in, item)) {
      ui_rect(a, item, 4, p->hover);
      a->cursor = CURSOR_HAND;
    }
    ui_text_fit(a, a->f.small, item.x + 8, item.y, 26, item.w - 16, i < a->vars.n ? a->vars.e[i].name : "Variables...",
                i == a->vars.active ? p->accent : p->text);
  }
}
