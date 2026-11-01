#include "app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern struct nk_vec2 g_space_origin;

#define CARD_GAP 8.0f
#define FIELD_H 28.0f
#define CMD_ROW 17.0f
#define CMD_MAX_ROWS 6
#define RECIPE_H 78.0f

/* ---- option cards ---- */

/* Writes a "--a, --b" list of other selected options this one conflicts with. */
static int conflict_text(const app_t *a, int opt, char *out, size_t n)
{
  const opt_def *o = &g_options[opt];
  int i, count = 0;
  size_t used = 0;
  out[0] = '\0';
  for(i = 0; i < a->req.nsel; i++) {
    const opt_def *other = &g_options[a->req.sel[i].opt];
    const char *const *m;
    int hit = 0;
    if(a->req.sel[i].opt == opt)
      continue;
    for(m = o->mutexes; m && *m; m++)
      if(!strcmp(*m, other->name))
        hit = 1;
    for(m = other->mutexes; m && *m; m++)
      if(!strcmp(*m, o->name))
        hit = 1;
    if(hit && used < n) {
      used += (size_t)snprintf(out + used, n - used, "%s--%s", count ? ", " : "", other->name);
      count++;
    }
  }
  return count;
}

/* Another chosen option that speaks a different protocol than this one. */
static int proto_clash(const app_t *a, int opt)
{
  int i;
  unsigned mine = options_proto_mask(opt);
  if(!mine)
    return -1;
  for(i = 0; i < a->req.nsel; i++) {
    unsigned other = options_proto_mask(a->req.sel[i].opt);
    if(a->req.sel[i].opt != opt && other && !(other & mine))
      return a->req.sel[i].opt;
  }
  return -1;
}

/* A value that looks like a URL while the URL bar is empty is almost certainly
   in the wrong place. --url itself is the right place. */
static int url_value_slot(const app_t *a, const sel_option *s)
{
  int j;
  if(a->req.url[0] || !strcmp(g_options[s->opt].name, "url"))
    return -1;
  for(j = 0; j < s->nvalues; j++)
    if(value_looks_like_url(s->values[j]))
      return j;
  return -1;
}

static float card_height(const app_t *a, const sel_option *s)
{
  const opt_def *o = &g_options[s->opt];
  char tmp[256];
  float h = 8 + 20 + 17 + 8;
  if(o->arg[0]) {
    h += (float)s->nvalues * (FIELD_H + 4);
    if(o->repeatable)
      h += 22;
    h += 4;
  }
  if(conflict_text(a, s->opt, tmp, sizeof(tmp)))
    h += 28;
  if(proto_clash(a, s->opt) >= 0)
    h += 28;
  if(!app_option_supported(a, s->opt))
    h += 28;
  if(url_value_slot(a, s) >= 0)
    h += 28;
  return h;
}

static void warn_bar(app_t *a, float x, float *y, float w, const char *msg)
{
  const palette *p = &a->pal;
  struct nk_rect bar = nk_rect(x, *y, w, 24);
  ui_rect(a, bar, 4, p->amber_bg);
  ui_rect(a, nk_rect(x, *y, 3, 24), 1, p->amber);
  ui_text_fit(a, a->f.small, x + 10, *y, 24, w - 16, msg, p->amber);
  *y += 28;
}

/* On / Off for a negatable flag. */
static void segmented(app_t *a, struct nk_rect r, int opt, int negated)
{
  const palette *p = &a->pal;
  struct nk_rect on = nk_rect(r.x, r.y, r.w / 2, r.h);
  struct nk_rect off = nk_rect(r.x + r.w / 2, r.y, r.w / 2, r.h);
  const struct nk_user_font *f = a->f.small;
  ui_rect(a, r, 4, p->bg);
  ui_rect(a, negated ? off : on, 4, negated ? p->border_strong : p->accent);
  ui_stroke(a, r, 4, p->border);
  ui_text(a, f, on.x + (on.w - ui_text_w(f, "On", 2)) / 2, r.y + (r.h - f->height) / 2, "On", 2,
          negated ? p->dim : p->on_accent);
  ui_text(a, f, off.x + (off.w - ui_text_w(f, "Off", 3)) / 2, r.y + (r.h - f->height) / 2, "Off", 3,
          negated ? p->text : p->dim);
  if(ui_hover(a, r))
    a->cursor = CURSOR_HAND;
  if(ui_clicked(a, on) && negated)
    app_push(a, ACT_SET_NEG, opt, 0);
  if(ui_clicked(a, off) && !negated)
    app_push(a, ACT_SET_NEG, opt, 1);
}

static void draw_card(app_t *a, struct nk_rect r, sel_option *s)
{
  const palette *p = &a->pal;
  const opt_def *o = &g_options[s->opt];
  float x = r.x + 12, w = r.w - 24, y = r.y + 8;
  char title[128], tmp[512];
  struct nk_rect close = nk_rect(r.x + r.w - 30, r.y + 6, 22, 22);
  float right = close.x - 6;
  float tw;
  int j;

  ui_rect(a, r, 7, p->card);
  ui_stroke(a, r, 7, p->border);
  if(ui_hover(a, r))
    ui_hover_doc(a, s->opt);

  snprintf(title, sizeof(title), "--%s", (s->negated && o->neg[0]) ? o->neg : o->name);
  tw = ui_text_w(a->f.mono_semi, title, (int)strlen(title));
  if(o->neg[0]) {
    struct nk_rect seg = nk_rect(right - 72, y, 72, 20);
    segmented(a, seg, s->opt, s->negated);
    right = seg.x - 8;
  }
  ui_text_fit(a, a->f.mono_semi, x, y, 20, right - x, title, s->negated ? p->dim : p->accent);
  if(o->short_flag[0] && x + tw + 34 < right) {
    snprintf(tmp, sizeof(tmp), "-%s", o->short_flag);
    ui_text_fit(a, a->f.mono_small, x + tw + 8, y, 20, 40, tmp, p->faint);
  }
  {
    int hc = ui_hover(a, close);
    if(hc) {
      ui_rect(a, close, 4, p->hover);
      a->cursor = CURSOR_HAND;
    }
    {
      struct nk_command_buffer *cv = nk_window_get_canvas(a->ctx);
      struct nk_color c = hc ? p->red : p->faint;
      float cx = close.x + 11, cy = close.y + 11, d = 4;
      nk_stroke_line(cv, cx - d, cy - d, cx + d, cy + d, 1.5f, c);
      nk_stroke_line(cv, cx - d, cy + d, cx + d, cy - d, 1.5f, c);
    }
    if(ui_clicked(a, close))
      app_push(a, ACT_REMOVE, s->opt, 0);
  }
  y += 20;
  ui_text_fit(a, a->f.small, x, y, 17, w, o->help, p->dim);
  y += 17 + 4;

  if(conflict_text(a, s->opt, tmp + 32, sizeof(tmp) - 32)) {
    memcpy(tmp, "Conflicts with ", 15);
    memmove(tmp + 15, tmp + 32, strlen(tmp + 32) + 1);
    warn_bar(a, x, &y, w, tmp);
  }
  {
    int other = proto_clash(a, s->opt);
    if(other >= 0) {
      snprintf(tmp, sizeof(tmp), "Used by other protocols than --%s: one of them will do nothing", g_options[other].name);
      warn_bar(a, x, &y, w, tmp);
    }
  }
  if(!app_option_supported(a, s->opt)) {
    snprintf(tmp, sizeof(tmp), "Your curl %s does not know this option; it needs %s or newer",
             a->curl.version, o->added);
    warn_bar(a, x, &y, w, tmp);
  }

  {
    int slot = url_value_slot(a, s);
    if(slot >= 0) {
      struct nk_rect use = nk_rect(x + w - 92, y + 2, 88, 20);
      warn_bar(a, x, &y, w - 96, "This looks like a URL, and the URL bar is empty");
      if(ui_button(a, use, "Use as URL", BTN_PRIMARY))
        app_push(a, ACT_USE_AS_URL, s->opt, slot);
    }
  }

  if(o->arg[0]) {
    for(j = 0; j < s->nvalues; j++) {
      float fw = w;
      nk_flags st;
      if(o->repeatable && s->nvalues > 1)
        fw -= 28;
      st = ui_edit_dyn(a, nk_rect(x, y, fw, FIELD_H), &s->values[j], &s->caps[j], o->placeholder,
                   a->f.mono_small, 0);
      if(st & NK_EDIT_ACTIVE)
        a->cards_focus = 1;
      if(o->repeatable && s->nvalues > 1) {
        struct nk_rect del = nk_rect(x + fw + 4, y + 3, 22, 22);
        int hd = ui_hover(a, del);
        struct nk_command_buffer *cv = nk_window_get_canvas(a->ctx);
        struct nk_color c = hd ? p->red : p->faint;
        float cx = del.x + 11, cy = del.y + 11, d = 3.5f;
        if(hd) {
          ui_rect(a, del, 4, p->hover);
          a->cursor = CURSOR_HAND;
        }
        nk_stroke_line(cv, cx - d, cy - d, cx + d, cy + d, 1.5f, c);
        nk_stroke_line(cv, cx - d, cy + d, cx + d, cy - d, 1.5f, c);
        if(ui_clicked(a, del))
          app_push(a, ACT_REMOVE_VALUE, s->opt, j);
      }
      y += FIELD_H + 4;
    }
    if(o->repeatable) {
      struct nk_rect add = nk_rect(x, y, ui_text_w(a->f.small, "+ add another value", 19) + 4, 20);
      int ha = ui_hover(a, add);
      ui_text(a, a->f.small, add.x, add.y + (20 - a->f.small->height) / 2, "+ add another value", 19,
              ha ? p->accent_hover : p->accent);
      if(ha)
        a->cursor = CURSOR_HAND;
      if(ui_clicked(a, add))
        app_push(a, ACT_ADD_VALUE, s->opt, 0);
    }
  }
}

/* ---- recipe cards ---- */

static void draw_recipe(app_t *a, struct nk_rect r, int index)
{
  const palette *p = &a->pal;
  const recipe *rc = &g_recipes[index];
  int hover = ui_hover(a, r);
  char flags[160];
  size_t n = 0;
  int i;
  ui_rect(a, r, 7, hover ? p->hover : p->card);
  ui_stroke(a, r, 7, hover ? p->accent : p->border);
  ui_text_fit(a, a->f.semi, r.x + 12, r.y + 8, 20, r.w - 24, rc->title, p->text);
  ui_text_fit(a, a->f.small, r.x + 12, r.y + 29, 17, r.w - 24, rc->desc, p->dim);
  flags[0] = '\0';
  for(i = 0; i < 5 && rc->opts[i].name && n < sizeof(flags); i++)
    n += (size_t)snprintf(flags + n, sizeof(flags) - n, "%s--%s", i ? " " : "", rc->opts[i].name);
  ui_text_fit(a, a->f.mono_small, r.x + 12, r.y + 50, 18, r.w - 24, flags, p->accent);
  if(hover) {
    a->cursor = CURSOR_HAND;
    ui_hover_doc(a, options_find(rc->opts[0].name));
    if(ui_clicked(a, r))
      app_push(a, ACT_RECIPE, index, 0);
  }
}

/* ---- the scrolling list in the middle ---- */

static void draw_list(app_t *a, struct nk_rect r)
{
  struct nk_context *ctx = a->ctx;
  struct nk_rect saved_clip = g_clip;
  struct nk_vec2 saved_origin = g_space_origin;
  int recipes = a->show_recipes || a->req.nsel == 0;
  float total = 0;
  int i;
  nk_uint locked_y = a->cards_y;

  a->cards_focus = 0;
  if(recipes)
    total = (float)((g_recipe_count + 1) / 2) * (RECIPE_H + CARD_GAP);
  else
    for(i = 0; i < a->req.nsel; i++)
      total += card_height(a, &a->req.sel[i]) + CARD_GAP;

  nk_layout_space_push(ctx, nk_rect(r.x - g_space_origin.x, r.y - g_space_origin.y, r.w, r.h));
  /* group backgrounds come from the window style: the panel colour here */
  nk_style_push_style_item(ctx, &ctx->style.window.fixed_background, nk_style_item_color(a->pal.bg));
  if(nk_group_scrolled_offset_begin(ctx, &a->cards_x, &a->cards_y, "cards", 0)) {
    struct nk_rect b, vis;
    nk_style_pop_style_item(ctx);
    nk_layout_space_begin(ctx, NK_STATIC, total > 1 ? total : 1, INT_MAX);
    /* nk_layout_space_bounds is the clip rect and ignores scrolling: the real
       origin of the content comes from to_screen */
    b = nk_layout_space_bounds(ctx);
    g_space_origin = nk_layout_space_to_screen(ctx, nk_vec2(0, 0));
    b.x = g_space_origin.x;
    b.y = g_space_origin.y;
    vis = ui_intersect(nk_window_get_panel(ctx)->clip, saved_clip);
    g_clip = vis;
    {
      float y = b.y;
      if(recipes) {
        float cw = (b.w - CARD_GAP) / 2;
        for(i = 0; i < g_recipe_count; i++) {
          float cx = b.x + (i % 2 ? cw + CARD_GAP : 0);
          float cy = b.y + (float)(i / 2) * (RECIPE_H + CARD_GAP);
          draw_recipe(a, nk_rect(cx, cy, cw, RECIPE_H), i);
        }
      }
      else {
        for(i = 0; i < a->req.nsel; i++) {
          float h = card_height(a, &a->req.sel[i]);
          draw_card(a, nk_rect(b.x, y, b.w, h), &a->req.sel[i]);
          y += h + CARD_GAP;
        }
      }
    }
    nk_layout_space_end(ctx);
    nk_group_scrolled_end(ctx);
  }
  else
    nk_style_pop_style_item(ctx);
  /* A nk_edit is identified by draw order, and an edit scrolled out of view
     is skipped, so scrolling would hand the focus to a neighbour. Hold the
     list still while one of its fields has the focus. */
  if(a->cards_focus)
    a->cards_y = locked_y;
  g_clip = saved_clip;
  g_space_origin = saved_origin;
  nk_push_scissor(nk_window_get_canvas(ctx), saved_clip);
}

/* ---- command card ---- */

static struct nk_color token_color(const palette *p, tok_kind k)
{
  switch(k) {
  case TOK_PROGRAM:
    return p->dim;
  case TOK_FLAG:
    return p->accent;
  case TOK_VALUE:
    return p->green;
  case TOK_URL:
    return p->text;
  }
  return p->text;
}

#define CMD_SHOW_MAX 2000u

/* What to draw for a token. A very long value is cut with its size: quoting and
   measuring all of it every frame would stall the window. Copy still has it all. */
static char *token_preview(const char *text)
{
  size_t n = strlen(text), cut = CMD_SHOW_MAX, qn;
  char *head, *q, *out;
  if(n <= CMD_SHOW_MAX)
    return shell_quote(text);
  while(cut && ((unsigned char)text[cut] & 0xC0) == 0x80)
    cut--;
  head = malloc(cut + 1);
  if(!head)
    return NULL;
  memcpy(head, text, cut);
  head[cut] = '\0';
  q = shell_quote(head);
  free(head);
  if(!q)
    return NULL;
  qn = strlen(q);
  out = malloc(qn + 64);
  if(out) {
    if(n >= 1048576)
      snprintf(out, qn + 64, "%s... (%.1f MB in all)", q, (double)n / 1048576.0);
    else
      snprintf(out, qn + 64, "%s... (%u KB in all)", q, (unsigned)(n / 1024));
  }
  free(q);
  return out;
}

/* Flows the tokens; returns the rows used. With draw set it also paints them
   and drives the docs panel from the flag under the mouse. */
static int layout_command(app_t *a, float x0, float y0, float w, int draw)
{
  const palette *p = &a->pal;
  const struct nk_user_font *f = a->f.mono_small;
  int cols = (int)(w / a->f.mono_small_w);
  int col = 0, row = 0, i;
  if(cols < 10)
    cols = 10;
  for(i = 0; i < a->args.count; i++) {
    const arg_token *t = &a->args.tok[i];
    char *q = token_preview(t->text);
    size_t len, off = 0;
    int glyphs = 0;
    size_t k;
    if(!q)
      continue;
    len = strlen(q);
    for(k = 0; k < len; k = utf8_next(q, len, k))
      glyphs++;
    if(col && col + 1 + glyphs > cols) {
      col = 0;
      row++;
    }
    else if(col)
      col++;
    while(off < len) {
      size_t end = off;
      int n = 0;
      while(end < len && col + n < cols) {
        end = utf8_next(q, len, end);
        n++;
      }
      if(draw && row < CMD_MAX_ROWS) {
        float tx = x0 + (float)col * a->f.mono_small_w;
        float ty = y0 + (float)row * CMD_ROW;
        struct nk_rect hit = nk_rect(tx, ty, (float)n * a->f.mono_small_w, CMD_ROW);
        ui_text(a, f, tx, ty + (CMD_ROW - f->height) / 2, q + off, (int)(end - off), token_color(p, t->kind));
        if(t->opt >= 0 && ui_hover(a, hit)) {
          ui_hover_doc(a, t->opt);
          nk_stroke_line(nk_window_get_canvas(a->ctx), hit.x, hit.y + CMD_ROW - 1, hit.x + hit.w,
                         hit.y + CMD_ROW - 1, 1.0f, token_color(p, t->kind));
        }
      }
      col += n;
      off = end;
      if(off < len) {
        col = 0;
        row++;
      }
    }
    free(q);
  }
  return row + 1;
}

static void draw_command(app_t *a, struct nk_rect r)
{
  const palette *p = &a->pal;
  struct nk_rect copy = nk_rect(r.x + r.w - 58, r.y + 5, 50, 22);
  int rows = layout_command(a, 0, 0, r.w - 24, 0);
  ui_rect(a, r, 7, p->card);
  ui_stroke(a, r, 7, p->border);
  ui_text_fit(a, a->f.semi, r.x + 12, r.y + 6, 20, 120, "Command", p->text);
  if(ui_button(a, copy, "Copy", BTN_SECONDARY)) {
    char *prev = builder_preview(&a->args);
    if(prev) {
      ui_copy(prev);
      free(prev);
    }
  }
  layout_command(a, r.x + 12, r.y + 31, r.w - 24, 1);
  if(rows > CMD_MAX_ROWS)
    ui_text_fit(a, a->f.small, r.x + r.w - 150, r.y + 6, 20, 88, "continues; Copy has it all", p->faint);
}

static float command_height(app_t *a, float w)
{
  int rows = layout_command(a, 0, 0, w - 24, 0);
  if(rows > CMD_MAX_ROWS)
    rows = CMD_MAX_ROWS;
  return 31 + (float)rows * CMD_ROW + 9;
}

/* ---- method menu ---- */

static struct nk_rect method_rect(float x, float y)
{
  return nk_rect(x, y, 88, 34);
}

/* The menu is the frame's only popup. It runs first, takes the input for
   itself, and draws last, so nothing underneath ever reacts through it. */
struct nk_rect center_menu_rect(app_t *a, struct nk_rect center)
{
  (void)a;
  return nk_rect(center.x + 12, center.y + 12 + 36, 110, (float)METHOD_COUNT * 26 + 8);
}

void center_draw_menu(app_t *a, struct nk_rect center)
{
  const palette *p = &a->pal;
  struct nk_rect m = center_menu_rect(a, center);
  const struct nk_input *in = &a->ctx->input;
  int i;
  ui_push_clip(a, nk_rect(0, 0, a->W, a->H));
  ui_rect(a, nk_rect(m.x + 1, m.y + 2, m.w, m.h), 7, nk_rgba(0, 0, 0, 60));
  ui_rect(a, m, 7, p->card);
  ui_stroke(a, m, 7, p->border_strong);
  for(i = 0; i < METHOD_COUNT; i++) {
    struct nk_rect item = nk_rect(m.x + 4, m.y + 4 + (float)i * 26, m.w - 8, 26);
    int hover = nk_input_is_mouse_hovering_rect(in, item);
    if(hover) {
      ui_rect(a, item, 4, p->hover);
      a->cursor = CURSOR_HAND;
    }
    ui_text(a, a->f.mono, item.x + 8, item.y + (26 - a->f.mono->height) / 2,
            i == 0 ? "Auto" : app_method_label(a, i), i == 0 ? 4 : (int)strlen(app_method_label(a, i)),
            i == a->method ? p->accent : p->text);
  }
}

int center_menu_item_at(app_t *a, struct nk_rect center, struct nk_vec2 pos)
{
  struct nk_rect m = center_menu_rect(a, center);
  int i;
  for(i = 0; i < METHOD_COUNT; i++)
    if(NK_INBOX(pos.x, pos.y, m.x + 4, m.y + 4 + (float)i * 26, m.w - 8, 26))
      return i;
  return -1;
}

/* ---- the column ---- */

void view_center(app_t *a, struct nk_rect r)
{
  const palette *p = &a->pal;
  struct nk_command_buffer *cv = nk_window_get_canvas(a->ctx);
  float x = r.x + 12, w = r.w - 24, y = r.y + 12;
  struct nk_rect mb = method_rect(x, y), run;
  nk_flags st;
  float cmd_h, list_h, out_h, avail;
  char tmp[64];
  int running = a->proc != NULL;

  ui_push_clip(a, r);

  /* request bar */
  {
    int hover = ui_hover(a, mb);
    const char *label = app_method_label(a, a->method);
    float cx = mb.x + mb.w - 15, cy = mb.y + 17;
    ui_rect(a, mb, 5, hover ? p->hover : p->card);
    ui_stroke(a, mb, 5, a->method_menu ? p->accent : p->border);
    ui_text(a, a->f.mono_semi, mb.x + 10, mb.y + (34 - a->f.mono_semi->height) / 2, label, (int)strlen(label),
            a->method ? p->text : p->dim);
    nk_fill_triangle(cv, cx - 4, cy - 2, cx + 4, cy - 2, cx, cy + 3, p->dim);
    if(hover)
      a->cursor = CURSOR_HAND;
    if(ui_clicked(a, mb))
      a->method_menu = !a->method_menu;
  }
  run = nk_rect(x + w - 76, y, 76, 34);
  st = ui_edit_dyn(a, nk_rect(mb.x + mb.w + 6, y, w - mb.w - 76 - 12, 34), &a->req.url, &a->req.url_cap,
               "https://example.com", a->f.mono, NK_EDIT_SIG_ENTER);
  if((st & NK_EDIT_COMMITED) && !running)
    app_push(a, ACT_RUN, 0, 0);
  if(a->req.url[0] || !request_lacks_url(&a->req))
    a->url_missing = 0;
  if(a->url_missing)
    ui_stroke(a, nk_rect(mb.x + mb.w + 6, y, w - mb.w - 76 - 12, 34), 5, p->amber);
  if(ui_button(a, run, running ? "Stop" : "Run", running ? BTN_DANGER : BTN_PRIMARY))
    app_push(a, running ? ACT_STOP : ACT_RUN, 0, 0);
  y += 42;

  /* options header */
  ui_text_fit(a, a->f.semi, x, y, 24, 70, "Options", p->text);
  if(a->req.nsel) {
    snprintf(tmp, sizeof(tmp), "%d", a->req.nsel);
    ui_pill(a, x + ui_text_w(a->f.semi, "Options", 7) + 8, y + 3, tmp, p->accent, p->accent_soft);
  }
  if(ui_button(a, nk_rect(x + w - 74, y, 74, 24), "Clear all", BTN_GHOST) && a->req.nsel)
    app_push(a, ACT_CLEAR, 0, 0);
  if(ui_button(a, nk_rect(x + w - 74 - 70, y, 66, 24), "Recipes", BTN_GHOST))
    a->show_recipes = !a->show_recipes;
  y += 30;

  /* Output height comes from the space really left, not a fixed share. */
  cmd_h = command_height(a, w);
  avail = r.y + r.h - y - 12 - CARD_GAP * 2 - cmd_h;
  out_h = NK_MAX(190, avail * 0.52f);
  list_h = avail - out_h;
  if(list_h < 110) {
    list_h = 110;
    out_h = avail - list_h;
  }

  draw_list(a, nk_rect(x, y, w, list_h));
  y += list_h + CARD_GAP;
  draw_command(a, nk_rect(x, y, w, cmd_h));
  y += cmd_h + CARD_GAP;
  view_output(a, nk_rect(x, y, w, out_h));
}
