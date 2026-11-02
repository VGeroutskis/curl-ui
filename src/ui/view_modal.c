/* The prompt for variables a request needs and the table does not have. */
#include "app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void modal_open_missing(app_t *a, str_list *names)
{
  int i, n = names->n;
  modal_close(a);
  a->modal_names = *names;
  names->s = NULL;
  names->n = 0;
  a->modal_values = calloc((size_t)n, sizeof(char *));
  a->modal_caps = calloc((size_t)n, sizeof(size_t));
  a->modal_secret = calloc((size_t)n, sizeof(int));
  for(i = 0; i < n; i++) {
    a->modal_caps[i] = 256;
    a->modal_values[i] = calloc(1, a->modal_caps[i]);
    {
      const var_t *known = var_find(app_tab_env(a, CUR(a)), a->modal_names.s[i]);
      a->modal_secret[i] = vars_suggest_secret(a->modal_names.s[i]) || (known && known->secret);
    }
  }
  a->modal_open = 1;
  a->pending_unfocus = 1;
}

void modal_close(app_t *a)
{
  int i;
  for(i = 0; i < a->modal_names.n; i++) {
    /* it may hold a secret that was never saved */
    if(a->modal_values && a->modal_values[i]) {
      memset(a->modal_values[i], 0, a->modal_caps[i]);
      free(a->modal_values[i]);
    }
  }
  free(a->modal_values);
  free(a->modal_caps);
  free(a->modal_secret);
  a->modal_values = NULL;
  a->modal_caps = NULL;
  a->modal_secret = NULL;
  str_list_free(&a->modal_names);
  a->modal_open = 0;
}

void modal_act(app_t *a, modal_action act)
{
  if(act == MODAL_SAVE) {
    var_env *e = app_tab_env(a, CUR(a));
    int i;
    for(i = 0; i < a->modal_names.n; i++)
      var_set(e, a->modal_names.s[i], a->modal_values[i], a->modal_secret[i]);
    app_vars_save(a);
  }
  modal_close(a);
  a->pending_unfocus = 1;
  if(act == MODAL_SAVE)
    app_run(a);
  else if(act == MODAL_OPEN_VARS) {
    app_open_vars(a, tab_coll(CUR(a)));
  }
}

void view_modal(app_t *a)
{
  const palette *p = &a->pal;
  int n = a->modal_names.n, i;
  float row = 40, w = NK_MIN(600, a->W - 80);
  float h = 112 + (float)n * row;
  float x, y;
  char tmp[160];
  modal_action act = MODAL_NONE;
  if(h > a->H - 60)
    h = a->H - 60;
  x = (a->W - w) / 2;
  y = (a->H - h) / 2;
  ui_push_clip(a, nk_rect(0, 0, a->W, a->H));
  ui_rect(a, nk_rect(0, 0, a->W, a->H), 0, nk_rgba(0, 0, 0, 120));
  ui_rect(a, nk_rect(x, y, w, h), 9, p->panel);
  ui_stroke(a, nk_rect(x, y, w, h), 9, p->border_strong);
  snprintf(tmp, sizeof(tmp), n == 1 ? "This request needs a variable" : "This request needs %d variables", n);
  ui_text_fit(a, a->f.title, x + 18, y + 12, 26, w - 36, tmp, p->text);
  {
    const coll_node *top = NULL;
    int c;
    for(c = 0; c < a->ncolls; c++)
      if(!strcmp(a->colls[c]->id, tab_coll(CUR(a))))
        top = a->colls[c]->root;
    snprintf(tmp, sizeof(tmp), "Saved in %.80s, environment %.40s", top ? top->name : "My requests", app_tab_env(a, CUR(a))->name);
  }
  ui_text_fit(a, a->f.small, x + 18, y + 38, 18, w - 36, tmp, p->dim);
  {
    float fy = y + 62, name_w = 150, sec_w = 74;
    for(i = 0; i < n && fy + row <= y + h - 44; i++) {
      struct nk_rect box = nk_rect(x + w - 18 - sec_w, fy + 7, 16, 16);
      struct nk_rect target = nk_rect(box.x, fy, sec_w, 30);
      int hover = ui_hover(a, target);
      ui_text_fit(a, a->f.mono_small, x + 18, fy, 30, name_w - 8, a->modal_names.s[i], p->amber);
      ui_edit_dyn(a, nk_rect(x + 18 + name_w, fy, w - 36 - name_w - sec_w - 8, 30), &a->modal_values[i],
                  &a->modal_caps[i], "value", a->f.mono_small, 0);
      ui_rect(a, box, 3, a->modal_secret[i] ? p->accent : p->field);
      ui_stroke(a, box, 3, a->modal_secret[i] ? p->accent : p->border_strong);
      ui_text_fit(a, a->f.small, box.x + 21, fy, 30, sec_w - 21, "Secret", p->dim);
      if(hover)
        a->cursor = CURSOR_HAND;
      if(ui_clicked(a, target))
        a->modal_secret[i] = !a->modal_secret[i];
      fy += row;
    }
  }
  {
    float by = y + h - 40;
    if(ui_button(a, nk_rect(x + w - 18 - 100, by, 100, 28), "Save & Run", BTN_PRIMARY))
      act = MODAL_SAVE;
    if(ui_button(a, nk_rect(x + w - 18 - 100 - 8 - 72, by, 72, 28), "Cancel", BTN_SECONDARY))
      act = MODAL_CANCEL;
    if(ui_button(a, nk_rect(x + 18, by, 110, 28), "Open Variables", BTN_GHOST))
      act = MODAL_OPEN_VARS;
  }
  if(act != MODAL_NONE)
    a->modal_pending = act;
}
