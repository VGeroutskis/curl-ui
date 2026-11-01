#include "app.h"

static struct nk_color hex(unsigned v)
{
  return nk_rgb((int)((v >> 16) & 255), (int)((v >> 8) & 255), (int)(v & 255));
}

static void load_palette(palette *p, int dark)
{
  if(dark) {
    p->bg = hex(0x12141A);       p->panel = hex(0x181B22);    p->card = hex(0x20242D);
    p->border = hex(0x2B303A);   p->border_strong = hex(0x3D4450);
    p->text = hex(0xE7E9EE);     p->dim = hex(0xA0A8B4);      p->faint = hex(0x707987);
    p->accent = hex(0x6CB6FF);   p->accent_hover = hex(0x8AC6FF);
    p->accent_soft = hex(0x1E2C3D); p->on_accent = hex(0x0A1420);
    p->green = hex(0x5CC98A);    p->amber = hex(0xE3B341);    p->amber_bg = hex(0x362C13);
    p->red = hex(0xF0716B);      p->field = hex(0x12141A);    p->hover = hex(0x262B35);
    p->code_bg = hex(0x12151B);
    p->j_key = hex(0x6CB6FF);    p->j_str = hex(0x5CC98A);
    p->j_num = hex(0xE3B341);    p->j_lit = hex(0xC89BF2);
  }
  else {
    p->bg = hex(0xF1F2F5);       p->panel = hex(0xFFFFFF);    p->card = hex(0xFFFFFF);
    p->border = hex(0xDEE1E7);   p->border_strong = hex(0xC2C7D0);
    p->text = hex(0x1B1F26);     p->dim = hex(0x565E6B);      p->faint = hex(0x7E8693);
    p->accent = hex(0x0B5FBF);   p->accent_hover = hex(0x094E9E);
    p->accent_soft = hex(0xE5EFFB); p->on_accent = hex(0xFFFFFF);
    p->green = hex(0x14774A);    p->amber = hex(0x7F5A00);    p->amber_bg = hex(0xFFF3D1);
    p->red = hex(0xBF2C27);      p->field = hex(0xF7F8FA);    p->hover = hex(0xEEF1F5);
    p->code_bg = hex(0xF3F4F7);
    p->j_key = hex(0x0B5FBF);    p->j_str = hex(0x14774A);
    p->j_num = hex(0x8F5A00);    p->j_lit = hex(0x7A3FB5);
  }
}

void theme_apply(app_t *a, int dark)
{
  struct nk_style *s = &a->ctx->style;
  palette *p = &a->pal;
  struct nk_style_edit *e = &s->edit;
  struct nk_style_scrollbar *sb = &s->scrollv;

  a->dark = dark;
  a->set.dark = dark;
  load_palette(p, dark);

  /* Everything is placed by hand: no padding or spacing anywhere. */
  s->window.padding = nk_vec2(0, 0);
  s->window.spacing = nk_vec2(0, 0);
  s->window.group_padding = nk_vec2(0, 0);
  s->window.scrollbar_size = nk_vec2(10, 10);
  s->window.min_row_height_padding = 0;
  s->window.border = 0;
  s->window.group_border = 0;
  s->window.fixed_background = nk_style_item_color(p->bg);
  s->window.background = p->bg;
  s->window.border_color = p->bg;
  s->window.group_border_color = p->bg;

  e->normal = nk_style_item_color(p->field);
  e->hover = nk_style_item_color(p->field);
  e->active = nk_style_item_color(p->field);
  e->border_color = p->border;
  e->border = 1;
  e->rounding = 5;
  e->padding = nk_vec2(8, 0);
  e->row_padding = 0;
  e->cursor_size = 1.5f;
  e->text_normal = p->text;
  e->text_hover = p->text;
  e->text_active = p->text;
  e->selected_normal = p->on_accent;
  e->selected_hover = p->on_accent;
  e->selected_text_normal = p->accent;
  e->selected_text_hover = p->accent;

  sb->normal = nk_style_item_color(UI_TRANSPARENT);
  sb->hover = nk_style_item_color(UI_TRANSPARENT);
  sb->active = nk_style_item_color(UI_TRANSPARENT);
  sb->cursor_normal = nk_style_item_color(p->border_strong);
  sb->cursor_hover = nk_style_item_color(p->faint);
  sb->cursor_active = nk_style_item_color(p->dim);
  sb->border = 0;
  sb->rounding = 4;
  sb->rounding_cursor = 4;
  sb->padding = nk_vec2(2, 2);
  sb->show_buttons = 0;
  sb->border_cursor = 0;
}
