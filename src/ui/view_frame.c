#include "app.h"

#include <SDL.h>
#include <stdio.h>
#include <string.h>

#define HEADER_H 48.0f
#define TABS_H 32.0f

extern struct nk_vec2 g_space_origin;
struct nk_rect center_menu_rect(app_t *a, struct nk_rect center);
void center_draw_menu(app_t *a, struct nk_rect center);
int center_menu_item_at(app_t *a, struct nk_rect center, struct nk_vec2 pos);

void view_header(app_t *a, struct nk_rect r)
{
  const palette *p = &a->pal;
  struct nk_command_buffer *cv = nk_window_get_canvas(a->ctx);
  float x = r.x + 14, cy = r.y + r.h / 2;
  char tmp[128];

  ui_push_clip(a, r);
  ui_rect(a, r, 0, p->panel);
  nk_stroke_line(cv, r.x, r.y + r.h - 0.5f, r.x + r.w, r.y + r.h - 0.5f, 1.0f, p->border);

  /* logo: the :// of a URL */
  ui_rect(a, nk_rect(x, cy - 13, 26, 26), 6, p->accent);
  {
    struct nk_color c = p->on_accent;
    float lx = x + 6;
    nk_fill_circle(cv, nk_rect(lx - 1, cy - 6, 3.4f, 3.4f), c);
    nk_fill_circle(cv, nk_rect(lx - 1, cy + 2.5f, 3.4f, 3.4f), c);
    nk_stroke_line(cv, lx + 9, cy - 7, lx + 4, cy + 7, 2.0f, c);
    nk_stroke_line(cv, lx + 15, cy - 7, lx + 10, cy + 7, 2.0f, c);
  }
  x += 34;
  ui_text(a, a->f.title, x, cy - a->f.title->height / 2, "curl UI", 7, p->text);
  x += ui_text_w(a->f.title, "curl UI", 7) + 14;
  snprintf(tmp, sizeof(tmp), "All %d options, explained", g_option_count);
  ui_text(a, a->f.sans, x, cy - a->f.sans->height / 2, tmp, (int)strlen(tmp), p->dim);

  /* right: version pill, theme */
  {
    float right = r.x + r.w - 14;
    struct nk_rect theme = nk_rect(right - 30, cy - 15, 30, 30);
    int ht = ui_hover(a, theme);
    float tx = theme.x + 15;
    ui_rect(a, theme, 6, ht ? p->hover : p->panel);
    ui_stroke(a, theme, 6, p->border);
    if(a->dark) {  /* a moon: light switches off */
      nk_fill_circle(cv, nk_rect(tx - 7, cy - 7, 14, 14), p->dim);
      nk_fill_circle(cv, nk_rect(tx - 3, cy - 9, 13, 13), ht ? p->hover : p->panel);
    }
    else {         /* a sun */
      int i;
      static const float dx[8] = { 1, 0.7f, 0, -0.7f, -1, -0.7f, 0, 0.7f };
      static const float dy[8] = { 0, 0.7f, 1, 0.7f, 0, -0.7f, -1, -0.7f };
      nk_fill_circle(cv, nk_rect(tx - 4, cy - 4, 8, 8), p->dim);
      for(i = 0; i < 8; i++)
        nk_stroke_line(cv, tx + dx[i] * 6.5f, cy + dy[i] * 6.5f, tx + dx[i] * 9, cy + dy[i] * 9, 1.4f, p->dim);
    }
    if(ht)
      a->cursor = CURSOR_HAND;
    if(ui_clicked(a, theme))
      theme_apply(a, !a->dark);

    if(a->curl.found)
      snprintf(tmp, sizeof(tmp), "curl %s", a->curl.version);
    else
      snprintf(tmp, sizeof(tmp), "curl not found");
    {
      float tw = ui_text_w(a->f.small, tmp, (int)strlen(tmp));
      struct nk_rect pill = nk_rect(theme.x - 8 - (tw + 30), cy - 13, tw + 30, 26);
      int hp = ui_hover(a, pill);
      ui_rect(a, pill, 13, hp ? p->hover : p->bg);
      ui_stroke(a, pill, 13, a->show_settings ? p->accent : p->border);
      nk_fill_circle(cv, nk_rect(pill.x + 9, cy - 3.5f, 7, 7), a->curl.found ? p->green : p->red);
      ui_text(a, a->f.small, pill.x + 21, cy - a->f.small->height / 2, tmp, (int)strlen(tmp), p->dim);
      if(hp)
        a->cursor = CURSOR_HAND;
      if(ui_clicked(a, pill)) {
        nk_edit_unfocus(a->ctx);
        a->show_settings = !a->show_settings;
        a->show_vars = 0;
      }
      {
        struct nk_rect gear = nk_rect(pill.x - 8 - 76, cy - 13, 76, 26);
        if(ui_button(a, gear, "Settings", a->show_settings ? BTN_PRIMARY : BTN_SECONDARY)) {
          nk_edit_unfocus(a->ctx);
          a->show_settings = !a->show_settings;
          a->show_vars = 0;
        }
        /* the environment: variables take their values from it */
        a->env_btn = nk_rect(gear.x - 8 - 170, cy - 13, 170, 26);
        {
          struct nk_rect e = a->env_btn;
          int he = ui_hover(a, e);
          float ax = e.x + e.w - 16, ay = cy - 2;
          ui_rect(a, e, 5, he ? p->hover : p->bg);
          ui_stroke(a, e, 5, a->show_vars ? p->accent : p->border);
          ui_text_fit(a, a->f.small, e.x + 9, e.y, e.h, e.w - 30, a->vars.e[a->vars.active].name, p->text);
          nk_fill_triangle(cv, ax - 4, ay, ax + 4, ay, ax, ay + 5, p->dim);
          if(he)
            a->cursor = CURSOR_HAND;
          if(ui_clicked(a, e))
            a->env_menu = !a->env_menu;
        }
      }
    }
  }
}

void view_frame(app_t *a)
{
  struct nk_context *ctx = a->ctx;
  float top = HEADER_H + TABS_H;
  float lib_w = NK_CLAMP(300, a->W * 0.24f, 380);
  float doc_w = NK_CLAMP(340, a->W * 0.29f, 480);
  struct nk_rect full = nk_rect(0, 0, a->W, a->H);
  struct nk_rect center = nk_rect(lib_w, top, a->W - lib_w - doc_w, a->H - top);
  struct nk_input *in = &ctx->input;
  struct nk_vec2 saved_pos = in->mouse.pos;
  struct nk_mouse_button saved_left = in->mouse.buttons[NK_BUTTON_LEFT];
  struct nk_vec2 saved_scroll = in->mouse.scroll_delta;
  int menu_was_open = CUR(a)->method_menu;
  int env_was_open = a->env_menu;
  int modal_was_open = a->modal_open || a->dlg != DLG_NONE;
  int tree_was_open = a->tree_menu != NULL;

  a->big_paste = 0;
  a->cursor = CURSOR_ARROW;
  a->hover_frame = -1;
  a->any_focus = 0;
  g_clip = full;
  g_space_origin = nk_vec2(0, 0);
  app_rebuild_args(a);

  /* The popup goes first and takes the input, so nothing below reacts. */
  if(a->show_settings)
    CUR(a)->method_menu = menu_was_open = 0;
  if(modal_was_open) {
    menu_was_open = env_was_open = tree_was_open = CUR(a)->method_menu = a->env_menu = 0;
    a->tree_menu = NULL;
  }
  if(menu_was_open || env_was_open || modal_was_open || tree_was_open) {
    const struct nk_mouse_button *b = &in->mouse.buttons[NK_BUTTON_LEFT];
    if(menu_was_open && b->clicked && !b->down) {
      int item = center_menu_item_at(a, center, in->mouse.pos);
      if(item >= 0)
        CUR(a)->method = item;
      CUR(a)->method_menu = 0;
    }
    if(env_was_open && b->clicked && !b->down) {
      env_menu_pick(a, env_menu_item_at(a, in->mouse.pos));
      a->env_menu = 0;
    }
    if(tree_was_open && b->clicked && !b->down)
      tree_menu_click(a, in->mouse.pos);
    in->mouse.pos = nk_vec2(-1000, -1000);
    in->mouse.buttons[NK_BUTTON_LEFT].clicked = 0;
    in->mouse.buttons[NK_BUTTON_LEFT].down = 0;
    in->mouse.scroll_delta = nk_vec2(0, 0);
  }

  if(nk_begin(ctx, "root", full, NK_WINDOW_NO_SCROLLBAR)) {
    if(a->pending_unfocus) {
      nk_edit_unfocus(ctx);
      a->pending_unfocus = 0;
    }
    nk_layout_space_begin(ctx, NK_STATIC, a->H, INT_MAX);
    view_header(a, nk_rect(0, 0, a->W, HEADER_H));
    view_tabs(a, nk_rect(0, HEADER_H, a->W, TABS_H));
    /* draw order is nk_edit identity: fixed edits come before the card list */
    view_sidebar(a, nk_rect(0, top, lib_w, a->H - top));
    if(a->notice[0]) {
      const palette *p = &a->pal;
      struct nk_rect bar = nk_rect(center.x, center.y, center.w, 30);
      ui_push_clip(a, bar);
      ui_rect(a, bar, 0, p->amber_bg);
      ui_text_fit(a, a->f.small, bar.x + 12, bar.y, bar.h, bar.w - 80, a->notice, p->amber);
      if(ui_button(a, nk_rect(bar.x + bar.w - 66, bar.y + 3, 60, 24), "Dismiss", BTN_GHOST))
        a->notice[0] = '\0';
      center.y += 30;
      center.h -= 30;
    }
    if(a->show_settings)
      view_settings(a, center);
    else if(a->show_vars)
      view_vars(a, center);
    else
      view_center(a, center);
    view_docs(a, nk_rect(a->W - doc_w, top, doc_w, a->H - top));
    if(menu_was_open && CUR(a)->method_menu) {
      in->mouse.pos = saved_pos;
      center_draw_menu(a, center);
    }
    if(env_was_open && a->env_menu) {
      in->mouse.pos = saved_pos;
      env_menu_draw(a);
    }
    if(tree_was_open && a->tree_menu) {
      in->mouse.pos = saved_pos;
      tree_menu_draw(a);
    }
    if(modal_was_open && (a->modal_open || a->dlg != DLG_NONE)) {
      in->mouse.pos = saved_pos;
      in->mouse.buttons[NK_BUTTON_LEFT] = saved_left;
      in->mouse.scroll_delta = saved_scroll;
      if(a->modal_open)
        view_modal(a);
      else
        view_dialog(a);
    }
    nk_layout_space_end(ctx);
  }
  nk_end(ctx);
  dialog_run_pending(a);
  app_tabs_update_dirty(a);
  if(a->modal_pending != MODAL_NONE) {
    modal_action act = a->modal_pending;
    a->modal_pending = MODAL_NONE;
    modal_act(a, act);
  }
  /* Blanking was for this frame's widgets only. Left blanked, the release of
     a held button would look like no change, and Nuklear would drop it. */
  in->mouse.pos = saved_pos;
  in->mouse.buttons[NK_BUTTON_LEFT] = saved_left;

  /* Docs follow the mouse, after a short dwell: moving across the middle
     column on the way to read them must not replace what is being read. */
  a->wants_timer = CUR(a)->copied_secret_at != 0;
  if(a->hover_frame != a->hover_cand) {
    a->hover_cand = a->hover_frame;
    a->hover_since = a->now_ms;
  }
  if(a->hover_cand >= 0 && a->hover_cand != a->doc_opt) {
    if(a->doc_opt < 0 || a->now_ms - a->hover_since >= (unsigned long)a->set.hover_ms)
      app_set_doc(a, a->hover_cand, 0);
    else
      a->wants_timer = 1;
  }
}
