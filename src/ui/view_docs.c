#include "app.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Colour roles: a laid out page survives a theme switch. */
enum { C_TEXT, C_DIM, C_FAINT, C_ACCENT, C_GREEN, C_AMBER, C_CODE_BG, C_SOFT, C_BORDER, C_CARD };

#define LINE_H 19.0f
#define CODE_LINE 16.0f
#define ARENA_CAP 8192
#define HEADER_H 62.0f

static struct nk_color role(const palette *p, int c)
{
  switch(c) {
  case C_DIM: return p->dim;
  case C_FAINT: return p->faint;
  case C_ACCENT: return p->accent;
  case C_GREEN: return p->green;
  case C_AMBER: return p->amber;
  case C_CODE_BG: return p->code_bg;
  case C_SOFT: return p->accent_soft;
  case C_BORDER: return p->border;
  case C_CARD: return p->card;
  default: return p->text;
  }
}

static dop *push_op(app_t *a)
{
  if(a->ndops == a->capdops) {
    int cap = a->capdops ? a->capdops * 2 : 512;
    dop *grown = realloc(a->dops, sizeof(dop) * (size_t)cap);
    if(!grown)
      return NULL;
    a->dops = grown;
    a->capdops = cap;
  }
  memset(&a->dops[a->ndops], 0, sizeof(dop));
  a->dops[a->ndops].opt = -1;
  return &a->dops[a->ndops++];
}

static const char *arena_fmt(app_t *a, const char *fmt, ...)
{
  va_list ap;
  char *out = a->doc_arena + a->doc_arena_n;
  size_t room = ARENA_CAP - a->doc_arena_n;
  int n;
  if(room < 2)
    return "";
  va_start(ap, fmt);
  n = vsnprintf(out, room, fmt, ap);
  va_end(ap);
  if(n < 0)
    return "";
  if((size_t)n >= room)
    n = (int)room - 1;
  a->doc_arena_n += (size_t)n + 1;
  return out;
}

static void op_text(app_t *a, dop_kind kind, float x, float y, const struct nk_user_font *f, int color,
                    const char *s, int len, int opt)
{
  dop *o = push_op(a);
  if(!o)
    return;
  o->kind = kind;
  o->x = x;
  o->y = y;
  o->font = f;
  o->color = color;
  o->s = s;
  o->len = len;
  o->opt = opt;
  o->w = ui_text_w(f, s, len);
  o->h = LINE_H;
}

static int op_rect(app_t *a, float x, float y, float w, float h, int color)
{
  dop *o = push_op(a);
  if(!o)
    return -1;
  o->kind = DOP_RECT;
  o->x = x;
  o->y = y;
  o->w = w;
  o->h = h;
  o->color = color;
  return a->ndops - 1;
}

/* Word wraps one block of spans at x0..width. Returns the y after it. */
static float flow_spans(app_t *a, const doc_block *b, float x0, float y, float width)
{
  float x = x0;
  int i;
  for(i = 0; i < b->nspans; i++) {
    const doc_span *sp = &b->spans[i];
    const struct nk_user_font *f = a->f.sans;
    int color = C_TEXT;
    const char *s = sp->text;
    float space;
    if(b->kind == BLOCK_BULLET || b->kind == BLOCK_PARA)
      color = C_TEXT;
    if(sp->kind == SPAN_BOLD)
      f = a->f.semi;
    else if(sp->kind == SPAN_CODE)
      f = a->f.mono_small;
    else if(sp->kind == SPAN_LINK) {
      f = a->f.mono_small;
      color = C_ACCENT;
    }
    space = ui_text_w(f, " ", 1);
    while(*s) {
      const char *e = s;
      float w;
      if(*s == ' ') {
        if(x > x0)
          x += space;
        s++;
        continue;
      }
      while(*e && *e != ' ')
        e++;
      w = ui_text_w(f, s, (int)(e - s));
      if(x + w > x0 + width && x > x0) {
        x = x0;
        y += LINE_H;
      }
      /* a word wider than the panel: cut it by character */
      while(w > width) {
        size_t n = 0, len = (size_t)(e - s);
        float acc = 0;
        while(n < len) {
          size_t nx = utf8_next(s, len, n);
          float cw = ui_text_w(f, s + n, (int)(nx - n));
          if(acc + cw > width)
            break;
          acc += cw;
          n = nx;
        }
        if(!n)
          break;
        op_text(a, DOP_TEXT, x, y, f, color, s, (int)n, -1);
        s += n;
        y += LINE_H;
        x = x0;
        w = ui_text_w(f, s, (int)(e - s));
      }
      if(sp->kind == SPAN_CODE || sp->kind == SPAN_LINK)
        op_rect(a, x - 2, y + 1, w + 4, LINE_H - 2, C_CODE_BG);
      op_text(a, sp->kind == SPAN_LINK ? DOP_LINK : DOP_TEXT, x, y, f, color, s, (int)(e - s), sp->opt);
      x += w;
      s = e;
    }
  }
  return y + LINE_H;
}

/* A code block: tinted panel, wrapped at the space closest to the edge. */
static float flow_code(app_t *a, const char *text, float y, float width)
{
  const struct nk_user_font *f = a->f.mono_small;
  float inner = width - 16;
  int cols = (int)(inner / a->f.mono_small_w);
  int bg = op_rect(a, 0, y, width, 0, C_CODE_BG);
  float start = y;
  const char *line = text;
  if(cols < 8)
    cols = 8;
  y += 6;
  for(;;) {
    const char *eol = strchr(line, '\n');
    size_t len = eol ? (size_t)(eol - line) : strlen(line), off = 0;
    if(!len)
      y += CODE_LINE;
    while(off < len) {
      size_t i = off, last_space = 0;
      int col = 0;
      while(i < len && col < cols) {
        if(line[i] == ' ')
          last_space = i + 1;
        i = utf8_next(line, len, i);
        col++;
      }
      if(i < len && last_space > off)
        i = last_space;
      op_text(a, DOP_TEXT, 8, y, f, C_TEXT, line + off, (int)(i - off), -1);
      a->dops[a->ndops - 1].h = CODE_LINE;
      y += CODE_LINE;
      off = i;
    }
    if(!eol)
      break;
    line = eol + 1;
  }
  y += 6;
  if(bg >= 0)
    a->dops[bg].h = y - start;
  return y;
}

static float section_title(app_t *a, const char *label, float y)
{
  op_text(a, DOP_TEXT, 0, y + 10, a->f.small, C_FAINT, label, (int)strlen(label), -1);
  return y + 32;
}

/* Flow of clickable --option chips. */
static float flow_chips(app_t *a, const char *const *names, float y, float width)
{
  float x = 0;
  for(; names && *names; names++) {
    int opt = options_find(*names);
    const char *label = arena_fmt(a, "--%s", *names);
    float w = ui_text_w(a->f.mono_small, label, (int)strlen(label)) + 14;
    if(x + w > width && x > 0) {
      x = 0;
      y += 26;
    }
    {
      dop *o = push_op(a);
      if(o) {
        o->kind = DOP_CHIP;
        o->x = x;
        o->y = y;
        o->w = w;
        o->h = 22;
        o->font = a->f.mono_small;
        o->color = C_ACCENT;
        o->s = label;
        o->len = (int)strlen(label);
        o->opt = opt;
      }
    }
    x += w + 4;
  }
  return y + 26;
}

static float flow_pill(app_t *a, const char *label, int color, float *x, float y, float width)
{
  float w = ui_pill_w(a, label);
  dop *o;
  if(*x + w > width && *x > 0) {
    *x = 0;
    y += 22;
  }
  o = push_op(a);
  if(o) {
    o->kind = DOP_PILL;
    o->x = *x;
    o->y = y;
    o->w = w;
    o->h = 18;
    o->font = a->f.small;
    o->color = color;
    o->s = label;
    o->len = (int)strlen(label);
  }
  *x += w + 4;
  return y;
}

static void build_page(app_t *a, int opt, float width)
{
  const opt_def *o = &g_options[opt];
  float y = 0, x = 0;
  int i;
  doc_block fake;
  doc_span span;

  a->ndops = 0;
  a->doc_arena_n = 0;
  docs_free(&a->doc);
  docs_parse(o->description, &a->doc);

  /* the one line help, wrapped, in bold */
  span.kind = SPAN_BOLD;
  span.text = (char *)o->help;
  span.opt = -1;
  fake.kind = BLOCK_PARA;
  fake.spans = &span;
  fake.nspans = 1;
  y = flow_spans(a, &fake, 0, y, width) + 6;

  /* pills */
  {
    const char *p = o->protocols;
    while(p && *p) {
      const char *e = strchr(p, ' ');
      int n = e ? (int)(e - p) : (int)strlen(p);
      y = flow_pill(a, arena_fmt(a, "%.*s", n, p), C_DIM, &x, y, width);
      p = e ? e + 1 : NULL;
    }
    y = flow_pill(a, arena_fmt(a, "since %s", o->added), C_DIM, &x, y, width);
    if(o->repeatable)
      y = flow_pill(a, "repeatable", C_DIM, &x, y, width);
    if(o->neg[0])
      y = flow_pill(a, arena_fmt(a, "--%s", o->neg), C_DIM, &x, y, width);
    if(o->deprecated)
      y = flow_pill(a, "deprecated", C_AMBER, &x, y, width);
    if(request_find(&CUR(a)->req, opt) >= 0)
      y = flow_pill(a, "in command", C_ACCENT, &x, y, width);
    if(!app_option_supported(a, opt))
      y = flow_pill(a, "not in your curl", C_AMBER, &x, y, width);
  }
  y += 28;

  for(i = 0; i < a->doc.nblocks; i++) {
    const doc_block *b = &a->doc.blocks[i];
    switch(b->kind) {
    case BLOCK_PARA:
      y = flow_spans(a, b, 0, y, width) + 8;
      break;
    case BLOCK_BULLET:
      op_text(a, DOP_TEXT, 3, y, a->f.sans, C_FAINT, "\xE2\x80\xA2", 3, -1);
      y = flow_spans(a, b, 14, y, width - 14) + 3;
      break;
    case BLOCK_TAG:
      if(b->nspans) {
        const char *t = b->spans[0].text;
        float w = ui_text_w(a->f.mono_small, t, (int)strlen(t));
        y += 4;
        op_rect(a, 0, y, NK_MIN(w + 12, width), 20, C_SOFT);
        op_text(a, DOP_TEXT, 6, y + 1, a->f.mono_small, C_ACCENT, t, (int)strlen(t), -1);
        y += 26;
      }
      break;
    case BLOCK_CODE:
      if(b->nspans)
        y = flow_code(a, b->spans[0].text, y, width) + 8;
      break;
    }
  }

  if(o->mutexes) {
    y = section_title(a, "CONFLICTS WITH", y + 2);
    y = flow_chips(a, o->mutexes, y, width);
  }
  if(o->examples) {
    y = section_title(a, "EXAMPLES", y + 2);
    for(i = 0; o->examples[i]; i++)
      y = flow_code(a, o->examples[i], y, width) + 5;
  }
  if(o->see_also) {
    y = section_title(a, "SEE ALSO", y + 2);
    y = flow_chips(a, o->see_also, y, width);
  }
  a->doc_content_h = y + 16;
}

static void draw_empty(app_t *a, struct nk_rect r)
{
  const palette *p = &a->pal;
  float x = r.x + 18, w = r.w - 36, y = r.y + 30;
  static const char *const lines[] = {
    "Hover any option in the library, a card",
    "or a flag in the command, and its full",
    "curl documentation appears here.",
    "",
    "The last one stays while you move over",
    "to read and scroll. Every --option in the",
    "text is a link, with a back arrow to",
    "return.",
  };
  int i;
  ui_text_fit(a, a->f.title, x, y, 24, w, "Docs follow your mouse", p->text);
  y += 34;
  for(i = 0; i < (int)(sizeof(lines) / sizeof(lines[0])); i++) {
    if(lines[i][0])
      ui_text_fit(a, a->f.sans, x, y, 20, w, lines[i], p->dim);
    y += lines[i][0] ? 20 : 10;
  }
}

void view_docs(app_t *a, struct nk_rect r)
{
  const palette *p = &a->pal;
  struct nk_command_buffer *cv = nk_window_get_canvas(a->ctx);
  float pad = 18;
  float content_w;
  const opt_def *o;
  int in_cmd, flags;
  struct nk_rect body;
  int i;

  ui_push_clip(a, r);
  ui_rect(a, r, 0, p->panel);
  nk_stroke_line(cv, r.x + 0.5f, r.y, r.x + 0.5f, r.y + r.h, 1.0f, p->border);

  if(a->doc_opt < 0) {
    draw_empty(a, r);
    return;
  }
  o = &g_options[a->doc_opt];
  in_cmd = request_find(&CUR(a)->req, a->doc_opt) >= 0;
  flags = in_cmd | (app_option_supported(a, a->doc_opt) ? 2 : 0);
  content_w = r.w - pad * 2 - 6;

  if(a->doc_built_opt != a->doc_opt || a->doc_built_w != content_w || a->doc_built_flags != flags) {
    build_page(a, a->doc_opt, content_w);
    a->doc_built_opt = a->doc_opt;
    a->doc_built_w = content_w;
    a->doc_built_flags = flags;
  }

  /* fixed header: back arrow, flag + argument, add/remove */
  {
    float x = r.x + pad, y = r.y + 12;
    char flag[128];
    float bw = in_cmd ? 74 : 118;
    struct nk_rect btn = nk_rect(r.x + r.w - pad - bw, y, bw, 26);
    if(a->doc_hist_n) {
      struct nk_rect back = nk_rect(x - 4, y + 1, 24, 24);
      int hb = ui_hover(a, back);
      float cx = back.x + 12, cy = back.y + 12;
      if(hb) {
        ui_rect(a, back, 4, p->hover);
        a->cursor = CURSOR_HAND;
      }
      nk_stroke_line(cv, cx + 3, cy - 5, cx - 3, cy, 1.8f, hb ? p->text : p->dim);
      nk_stroke_line(cv, cx - 3, cy, cx + 3, cy + 5, 1.8f, hb ? p->text : p->dim);
      if(ui_clicked(a, back))
        app_doc_back(a);
      x += 24;
    }
    if(ui_button(a, btn, in_cmd ? "Remove" : "+ Add to command", in_cmd ? BTN_SECONDARY : BTN_PRIMARY))
      app_push(a, ACT_TOGGLE, a->doc_opt, 0);
    if(o->short_flag[0])
      snprintf(flag, sizeof(flag), "-%s, --%s", o->short_flag, o->name);
    else
      snprintf(flag, sizeof(flag), "--%s", o->name);
    if(btn.x - 8 - x < ui_text_w(a->f.mono_semi, flag, (int)strlen(flag)))
      snprintf(flag, sizeof(flag), "--%s", o->name);
    ui_text_fit(a, a->f.mono_semi, x, y, 26, btn.x - 8 - x, flag, p->accent);
    if(o->arg[0])
      ui_text_fit(a, a->f.mono_small, r.x + pad, y + 28, 16, r.w - pad * 2, o->arg, p->faint);
  }
  nk_stroke_line(cv, r.x + 1, r.y + HEADER_H - 0.5f, r.x + r.w, r.y + HEADER_H - 0.5f, 1.0f, p->border);

  /* the page */
  body = nk_rect(r.x + 1, r.y + HEADER_H, r.w - 1, r.h - HEADER_H);
  ui_clamp_scroll(&a->doc_scroll, a->doc_content_h + 12, body.h);
  ui_wheel(a, body, &a->doc_scroll, a->doc_content_h + 12, 50);
  ui_push_clip(a, body);
  {
    float ox = r.x + pad, oy = body.y + 12 - a->doc_scroll;
    float top = body.y - 40, bottom = body.y + body.h + 4;
    for(i = 0; i < a->ndops; i++) {
      const dop *d = &a->dops[i];
      float dx = ox + d->x, dy = oy + d->y;
      struct nk_rect hit = nk_rect(dx, dy, d->w, d->h);
      if(dy > bottom || dy + d->h < top)
        continue;
      switch(d->kind) {
      case DOP_RECT:
        ui_rect(a, hit, 3, role(p, d->color));
        break;
      case DOP_TEXT:
        ui_text(a, d->font, dx, dy + (d->h - d->font->height) / 2, d->s, d->len, role(p, d->color));
        break;
      case DOP_LINK: {
        int hl = ui_hover(a, hit);
        ui_text(a, d->font, dx, dy + (d->h - d->font->height) / 2, d->s, d->len, hl ? p->accent_hover : p->accent);
        if(hl) {
          nk_stroke_line(cv, dx, dy + d->h - 2, dx + d->w, dy + d->h - 2, 1.0f, p->accent_hover);
          a->cursor = CURSOR_HAND;
          if(ui_clicked(a, hit) && d->opt >= 0)
            app_set_doc(a, d->opt, 1);
        }
        break;
      }
      case DOP_CHIP: {
        int hc = ui_hover(a, hit);
        ui_rect(a, hit, 5, hc ? p->hover : p->code_bg);
        ui_stroke(a, hit, 5, hc ? p->accent : p->border);
        ui_text(a, d->font, dx + 7, dy + (d->h - d->font->height) / 2, d->s, d->len,
                d->opt >= 0 ? p->accent : p->dim);
        if(hc) {
          a->cursor = CURSOR_HAND;
          if(ui_clicked(a, hit) && d->opt >= 0)
            app_set_doc(a, d->opt, 1);
        }
        break;
      }
      case DOP_PILL:
        ui_rect(a, hit, 9, d->color == C_DIM ? p->code_bg : (d->color == C_AMBER ? p->amber_bg : p->accent_soft));
        ui_text(a, d->font, dx + 6, dy + (d->h - d->font->height) / 2, d->s, d->len, role(p, d->color));
        break;
      }
    }
  }
  ui_scrollbar(a, body, &a->doc_scroll, a->doc_content_h + 12);
}
