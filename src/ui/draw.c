#include "app.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct nk_rect g_clip;
struct nk_vec2 g_space_origin;

struct nk_rect ui_intersect(struct nk_rect p, struct nk_rect q)
{
  float x0 = NK_MAX(p.x, q.x), y0 = NK_MAX(p.y, q.y);
  float x1 = NK_MIN(p.x + p.w, q.x + q.w), y1 = NK_MIN(p.y + p.h, q.y + q.h);
  return nk_rect(x0, y0, NK_MAX(0, x1 - x0), NK_MAX(0, y1 - y0));
}

/* Hit tests are decided against what is visible now, and never see through a
   popup: view_frame blanks the input while one is open. */
int ui_hover(app_t *a, struct nk_rect r)
{
  struct nk_rect v = ui_intersect(r, g_clip);
  if(v.w <= 0 || v.h <= 0)
    return 0;
  if(a->ctx->current && a->ctx->current->layout &&
     (a->ctx->current->layout->flags & NK_WINDOW_ROM))
    return 0;
  if(a->drag_scroll)
    return 0;
  return nk_input_is_mouse_hovering_rect(&a->ctx->input, v);
}

int ui_clicked(app_t *a, struct nk_rect r)
{
  const struct nk_mouse_button *b = &a->ctx->input.mouse.buttons[NK_BUTTON_LEFT];
  if(!ui_hover(a, r))
    return 0;
  /* a click is a release, on the thing the press started on */
  return b->clicked && !b->down && NK_INBOX(b->clicked_pos.x, b->clicked_pos.y, r.x, r.y, r.w, r.h);
}

float ui_text_w(const struct nk_user_font *f, const char *s, int len)
{
  return f->width(f->userdata, f->height, s, len);
}

void ui_text(app_t *a, const struct nk_user_font *f, float x, float y, const char *s, int len,
             struct nk_color c)
{
  struct nk_command_buffer *cv = nk_window_get_canvas(a->ctx);
  if(len <= 0)
    return;
  /* a whole-pixel origin keeps pixel-snapped glyphs crisp; generous width so
     nk_draw_text never cuts the string itself */
  nk_draw_text(cv, nk_rect((float)(int)(x + 0.5f), (float)(int)(y + 0.5f), 8192, f->height), s, len, f,
               UI_TRANSPARENT, c);
}

size_t utf8_next(const char *s, size_t len, size_t i)
{
  i++;
  while(i < len && ((unsigned char)s[i] & 0xC0) == 0x80)
    i++;
  return i;
}

void ui_text_fit(app_t *a, const struct nk_user_font *f, float x, float y, float h, float max_w,
                 const char *s, struct nk_color c)
{
  static const char ell[] = "\xE2\x80\xA6";
  int len = (int)strlen(s);
  float tw;
  float ty = y + (h - f->height) * 0.5f;
  if(len > 512)
    len = 512;
  tw = ui_text_w(f, s, len);
  if(tw <= max_w) {
    ui_text(a, f, x, ty, s, len, c);
    return;
  }
  {
    float ew = ui_text_w(f, ell, 3), acc = 0;
    size_t i = 0, n = (size_t)len;
    while(i < n) {
      size_t j = utf8_next(s, n, i);
      float w = ui_text_w(f, s + i, (int)(j - i));
      if(acc + w + ew > max_w)
        break;
      acc += w;
      i = j;
    }
    ui_text(a, f, x, ty, s, (int)i, c);
    ui_text(a, f, x + acc, ty, ell, 3, c);
  }
}

void ui_rect(app_t *a, struct nk_rect r, float round, struct nk_color fill)
{
  nk_fill_rect(nk_window_get_canvas(a->ctx), r, round, fill);
}

void ui_stroke(app_t *a, struct nk_rect r, float round, struct nk_color c)
{
  nk_stroke_rect(nk_window_get_canvas(a->ctx), r, round, 1.0f, c);
}

void ui_push_clip(app_t *a, struct nk_rect r)
{
  g_clip = r;
  nk_push_scissor(nk_window_get_canvas(a->ctx), r);
}

int ui_button(app_t *a, struct nk_rect r, const char *label, btn_style style)
{
  const palette *p = &a->pal;
  int hover = ui_hover(a, r);
  struct nk_color fg = p->text, bg = UI_TRANSPARENT, border = UI_TRANSPARENT;
  const struct nk_user_font *f = a->f.semi;
  float tw;
  switch(style) {
  case BTN_PRIMARY:
    bg = hover ? p->accent_hover : p->accent;
    fg = p->on_accent;
    break;
  case BTN_DANGER:
    bg = p->red;
    fg = p->on_accent;
    break;
  case BTN_SECONDARY:
    bg = hover ? p->hover : p->card;
    border = hover ? p->border_strong : p->border;
    f = a->f.sans;
    break;
  case BTN_GHOST:
    bg = hover ? p->hover : UI_TRANSPARENT;
    fg = hover ? p->text : p->dim;
    f = a->f.sans;
    break;
  }
  if(bg.a)
    ui_rect(a, r, 5, bg);
  if(border.a)
    ui_stroke(a, r, 5, border);
  tw = ui_text_w(f, label, (int)strlen(label));
  ui_text(a, f, r.x + (r.w - tw) * 0.5f, r.y + (r.h - f->height) * 0.5f, label, (int)strlen(label), fg);
  if(hover)
    a->cursor = CURSOR_HAND;
  return ui_clicked(a, r);
}

float ui_pill_w(app_t *a, const char *label)
{
  return ui_text_w(a->f.small, label, (int)strlen(label)) + 12;
}

float ui_pill(app_t *a, float x, float y, const char *label, struct nk_color fg, struct nk_color bg)
{
  float w = ui_pill_w(a, label);
  ui_rect(a, nk_rect(x, y, w, 18), 9, bg);
  ui_text(a, a->f.small, x + 6, y + (18 - a->f.small->height) * 0.5f, label, (int)strlen(label), fg);
  return w;
}

void ui_clamp_scroll(float *scroll, float content, float view)
{
  float max = content - view;
  if(max < 0)
    max = 0;
  if(*scroll > max)
    *scroll = max;
  if(*scroll < 0)
    *scroll = 0;
}

int ui_wheel(app_t *a, struct nk_rect r, float *scroll, float content, float step)
{
  float dy = a->ctx->input.mouse.scroll_delta.y;
  if(dy == 0 || !ui_hover(a, r))
    return 0;
  *scroll -= dy * step;
  ui_clamp_scroll(scroll, content, r.h);
  return 1;
}

/* Thin custom scrollbar for the hand-virtualized panels. Dragging is captured
   so the thumb follows the mouse anywhere on screen. */
void ui_scrollbar(app_t *a, struct nk_rect track, float *scroll, float content)
{
  const palette *p = &a->pal;
  const struct nk_input *in = &a->ctx->input;
  float view = track.h, th, ty, range;
  struct nk_rect thumb;
  int dragging = a->drag_scroll == scroll, hover;
  if(content <= view + 0.5f)
    return;
  th = NK_MAX(24, view * view / content);
  range = view - th;
  ty = track.y + range * (*scroll / (content - view));
  thumb = nk_rect(track.x + track.w - 8, ty, 6, th);
  if(dragging) {
    if(!in->mouse.buttons[NK_BUTTON_LEFT].down) {
      a->drag_scroll = NULL;
      dragging = 0;
    }
    else if(range > 0) {
      *scroll = (in->mouse.pos.y - a->drag_grab - track.y) / range * (content - view);
      ui_clamp_scroll(scroll, content, view);
    }
  }
  /* only the strip at the edge belongs to the bar: the rest of the rect is
     the panel's content, which has its own clicks */
  hover = !a->drag_scroll && ui_hover(a, nk_rect(track.x + track.w - 11, track.y, 11, track.h));
  if(hover && in->mouse.buttons[NK_BUTTON_LEFT].down && in->mouse.buttons[NK_BUTTON_LEFT].clicked) {
    float my = in->mouse.pos.y;
    if(my < ty || my > ty + th) {  /* click on the track jumps there */
      *scroll = (my - th * 0.5f - track.y) / NK_MAX(1, range) * (content - view);
      ui_clamp_scroll(scroll, content, view);
      ty = track.y + range * (*scroll / (content - view));
      thumb.y = ty;
    }
    a->drag_scroll = scroll;
    a->drag_grab = my - ty;
    dragging = 1;
  }
  ui_rect(a, thumb, 3, dragging ? p->dim : (hover ? p->faint : p->border_strong));
}

nk_flags ui_edit(app_t *a, struct nk_rect r, char *buf, int cap, const char *placeholder,
                 const struct nk_user_font *font, nk_flags extra)
{
  struct nk_context *ctx = a->ctx;
  struct nk_style_edit *e = &ctx->style.edit;
  const palette *p = &a->pal;
  extern struct nk_vec2 g_space_origin;
  struct nk_color caret = a->blink_off ? UI_TRANSPARENT : p->text;
  nk_flags state;

  e->cursor_normal = caret;
  e->cursor_hover = caret;
  e->cursor_text_normal = p->text;
  e->cursor_text_hover = p->text;
  nk_style_push_font(ctx, font);
  nk_layout_space_push(ctx, nk_rect(r.x - g_space_origin.x, r.y - g_space_origin.y, r.w, r.h));
  state = nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD | extra, buf, cap, nk_filter_default);
  nk_style_pop_font(ctx);
  if(state & NK_EDIT_ACTIVE) {
    a->any_focus = 1;
    ui_stroke(a, r, 5, p->accent);
  }
  if(!buf[0] && placeholder && placeholder[0])
    ui_text_fit(a, font, r.x + 9, r.y, r.h, r.w - 16, placeholder, p->faint);
  if(ui_hover(a, r))
    a->cursor = CURSOR_IBEAM;
  return state;
}

static void paste_or_defer(nk_handle usr, struct nk_text_edit *edit)
{
  app_t *a = usr.ptr;
  char *text = SDL_GetClipboardText();
  if(text) {
    if(strlen(text) > FIELD_EDIT_MAX)
      a->big_paste = 1;
    else
      nk_textedit_paste(edit, text, nk_strlen(text));
    SDL_free(text);
  }
}

void ui_install_paste(app_t *a)
{
  a->ctx->clip.paste = paste_or_defer;
  a->ctx->clip.userdata = nk_handle_ptr(a);
}

static int reserve(char **buf, size_t *cap, size_t want)
{
  char *grown;
  if(want <= *cap)
    return 0;
  grown = realloc(*buf, want);
  if(!grown)
    return -1;
  *buf = grown;
  *cap = want;
  return 0;
}

/* Puts the clipboard at the end of buf, or over it. */
static void take_clipboard(char **buf, size_t *cap, int replace)
{
  char *text = SDL_GetClipboardText();
  if(text) {
    size_t have = replace ? 0 : strlen(*buf), n = strlen(text);
    if(!reserve(buf, cap, have + n + 1))
      memcpy(*buf + have, text, n + 1);
    SDL_free(text);
  }
}

static void draw_large_value(app_t *a, struct nk_rect r, char **buf, size_t *cap, size_t len)
{
  const palette *p = &a->pal;
  char label[64];
  float bh = r.h - 6;
  struct nk_rect clear = nk_rect(r.x + r.w - 53, r.y + 3, 50, bh);
  struct nk_rect replace = nk_rect(clear.x - 164, r.y + 3, 160, bh);
  if(len >= 1048576)
    snprintf(label, sizeof(label), "%.1f MB of text", (double)len / 1048576.0);
  else
    snprintf(label, sizeof(label), "%u KB of text", (unsigned)(len / 1024));
  ui_rect(a, r, 5, p->field);
  ui_stroke(a, r, 5, p->border);
  ui_text_fit(a, a->f.small, r.x + 9, r.y, r.h, r.w - 240, label, p->dim);
  if(ui_button(a, replace, "Replace from clipboard", BTN_SECONDARY))
    take_clipboard(buf, cap, 1);
  if(ui_button(a, clear, "Clear", BTN_SECONDARY))
    (*buf)[0] = '\0';
}

nk_flags ui_edit_dyn(app_t *a, struct nk_rect r, char **buf, size_t *cap, const char *placeholder,
                     const struct nk_user_font *font, nk_flags extra)
{
  size_t len = strlen(*buf);
  nk_flags state;
  if(len > FIELD_EDIT_MAX) {
    draw_large_value(a, r, buf, cap, len);
    return 0;
  }
  reserve(buf, cap, len + FIELD_EDIT_MAX + 4096);
  state = ui_edit(a, r, *buf, (int)*cap, placeholder, font, extra);
  if(a->big_paste && (state & NK_EDIT_ACTIVE)) {
    take_clipboard(buf, cap, 0);
    a->big_paste = 0;
  }
  return state;
}

void ui_hover_doc(app_t *a, int opt)
{
  a->hover_frame = opt;
}

void ui_copy(const char *text)
{
  SDL_SetClipboardText(text);
}
