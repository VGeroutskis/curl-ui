#include "app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ROW_H 17.0f

/* ---- wrap cache ---- */

static void add_row(wrap_cache *w, unsigned line, unsigned off, unsigned len)
{
  if(w->nrows == w->cap) {
    size_t cap = w->cap ? w->cap * 2 : 1024;
    wrap_row *grown = realloc(w->rows, cap * sizeof(wrap_row));
    if(!grown)
      return;
    w->rows = grown;
    w->cap = cap;
  }
  w->rows[w->nrows].line = line;
  w->rows[w->nrows].off = off;
  w->rows[w->nrows].len = len;
  w->nrows++;
}

static void wrap_line(wrap_cache *w, unsigned line, const char *s, size_t len)
{
  size_t start = 0;
  if(!len) {
    add_row(w, line, 0, 0);
    return;
  }
  while(start < len) {
    size_t i = start, last_space = 0;
    int col = 0;
    while(i < len && col < w->cols) {
      if(s[i] == ' ')
        last_space = i + 1;
      i = utf8_next(s, len, i);
      col++;
    }
    if(i < len && last_space > start + 1 && last_space - start > (i - start) / 2)
      i = last_space;  /* break at a space when one is close enough */
    add_row(w, line, (unsigned)start, (unsigned)(i - start));
    start = i;
  }
}

/* Only new text is wrapped while output streams. The last line may have grown
   since last time, so it is always redone. */
static void wrap_update(wrap_cache *w, outbuf *src, int cols)
{
  unsigned long gen = outbuf_generation(src);
  size_t count, i;
  if(w->src == src && w->cols == cols && w->gen == gen)
    return;
  outbuf_lock(src);
  count = outbuf_line_count_locked(src);
  if(w->src != src || w->cols != cols || count < w->lines_done) {
    w->src = src;
    w->cols = cols;
    w->lines_done = 0;
    w->nrows = 0;
  }
  if(w->lines_done) {
    unsigned last = (unsigned)(w->lines_done - 1);
    while(w->nrows && w->rows[w->nrows - 1].line == last)
      w->nrows--;
    w->lines_done--;
  }
  for(i = w->lines_done; i < count; i++) {
    size_t len;
    const char *s = outbuf_line_locked(src, i, &len);
    wrap_line(w, (unsigned)i, s, len);
  }
  w->lines_done = count;
  w->gen = gen;
  outbuf_unlock(src);
}

/* ---- colouring ---- */

static struct nk_color log_color(const palette *p, const char *s, size_t len)
{
  if(len >= 2 && s[0] == '*' && s[1] == ' ')
    return p->faint;
  if(len >= 2 && s[0] == '>' && s[1] == ' ')
    return p->accent;
  if(len >= 2 && s[0] == '<' && s[1] == ' ')
    return p->green;
  if(len >= 5 && !memcmp(s, "curl:", 5))
    return p->red;
  return p->dim;
}

static void draw_span(app_t *a, float x0, float y, const wrap_row *row, const char *line, size_t from, size_t to,
                      struct nk_color c)
{
  size_t start = NK_MAX(from, row->off), end = NK_MIN(to, (size_t)row->off + row->len), k;
  int col = 0;
  if(start >= end)
    return;
  for(k = row->off; k < start; k = utf8_next(line, start, k))
    col++;
  ui_text(a, a->f.mono_small, x0 + (float)col * a->f.mono_small_w, y, line + start, (int)(end - start), c);
}

static void draw_json_row(app_t *a, float x, float y, const wrap_row *row, const char *s, size_t len)
{
  const palette *p = &a->pal;
  size_t i = 0;
  while(i < len) {
    size_t st = i;
    char c = s[i];
    struct nk_color col = p->dim;
    if(c == '"') {
      i++;
      while(i < len && s[i] != '"') {
        if(s[i] == '\\')
          i++;
        i++;
      }
      if(i < len)
        i++;
      if(i > len)
        i = len;
      {
        size_t k = i;
        while(k < len && s[k] == ' ')
          k++;
        col = (k < len && s[k] == ':') ? p->j_key : p->j_str;
      }
    }
    else if(c == '-' || (c >= '0' && c <= '9')) {
      while(i < len && s[i] != ',' && s[i] != ' ' && s[i] != ']' && s[i] != '}')
        i++;
      col = p->j_num;
    }
    else if(c == 't' || c == 'f' || c == 'n') {
      while(i < len && s[i] >= 'a' && s[i] <= 'z')
        i++;
      col = p->j_lit;
    }
    else {
      i++;
      while(i < len && s[i] != '"' && !(s[i] >= '0' && s[i] <= '9') && s[i] != '-' && s[i] != 't' &&
            s[i] != 'f' && s[i] != 'n')
        i++;
    }
    draw_span(a, x, y, row, s, st, i, col);
  }
}

/* ---- chips and the toolbar ---- */

static void format_size(long long n, char *out, size_t len)
{
  if(n < 1024)
    snprintf(out, len, "%lld B", n);
  else if(n < 1024 * 1024)
    snprintf(out, len, "%.1f KB", (double)n / 1024.0);
  else
    snprintf(out, len, "%.1f MB", (double)n / (1024.0 * 1024.0));
}

static int draw_tab(app_t *a, float x, float y, const char *label, int active, float *adv)
{
  const palette *p = &a->pal;
  float w = ui_text_w(a->f.semi, label, (int)strlen(label)) + 18;
  struct nk_rect r = nk_rect(x, y, w, 30);
  int hover = ui_hover(a, r);
  ui_text(a, a->f.semi, x + 9, y + (30 - a->f.semi->height) / 2, label, (int)strlen(label),
          active ? p->text : (hover ? p->text : p->dim));
  if(active)
    ui_rect(a, nk_rect(x + 6, y + 28, w - 12, 2), 1, p->accent);
  if(hover)
    a->cursor = CURSOR_HAND;
  *adv = w;
  return ui_clicked(a, r);
}

static void empty_state(app_t *a, struct nk_rect r, const char *title, const char *hint)
{
  const palette *p = &a->pal;
  float cy = r.y + r.h * 0.5f - 22;
  float tw = ui_text_w(a->f.semi, title, (int)strlen(title));
  float hw = ui_text_w(a->f.small, hint, (int)strlen(hint));
  ui_text(a, a->f.semi, r.x + (r.w - tw) / 2, cy, title, (int)strlen(title), p->dim);
  ui_text(a, a->f.small, r.x + NK_MAX(8, (r.w - hw) / 2), cy + 22, hint, (int)strlen(hint), p->faint);
}

void view_output(app_t *a, struct nk_rect r)
{
  const palette *p = &a->pal;
  struct nk_rect body;
  float x = r.x + 6, adv, right = r.x + r.w - 8, chips_x;
  int running = CUR(a)->proc != NULL;
  int showing_pretty = CUR(a)->out_tab == TAB_RESPONSE && CUR(a)->pretty && CUR(a)->pretty_valid;
  outbuf *src = CUR(a)->out_tab == TAB_LOG ? CUR(a)->log : (showing_pretty ? CUR(a)->pretty_buf : CUR(a)->out);
  wrap_cache *wc = CUR(a)->out_tab == TAB_LOG ? &CUR(a)->wrap_log : (showing_pretty ? &CUR(a)->wrap_pretty : &CUR(a)->wrap_out);
  char tmp[64];
  int cols;
  float content;

  ui_rect(a, r, 7, p->card);
  ui_stroke(a, r, 7, p->border);

  if(draw_tab(a, x, r.y + 1, "Response", CUR(a)->out_tab == TAB_RESPONSE, &adv))
    CUR(a)->out_tab = TAB_RESPONSE;
  x += adv;
  if(draw_tab(a, x, r.y + 1, "Log", CUR(a)->out_tab == TAB_LOG, &adv))
    CUR(a)->out_tab = TAB_LOG;
  x += adv + 8;
  chips_x = x;

  /* right side tools first: they are what must never be squeezed out */
  right -= 44;
  if(ui_button(a, nk_rect(right, r.y + 5, 44, 22), "Clear", BTN_GHOST) && !running)
    app_clear_output(a);
  right -= 46;
  if(ui_button(a, nk_rect(right, r.y + 5, 44, 22), "Copy", BTN_GHOST)) {
    size_t text_len = 0;
    char *text = outbuf_join(src, &text_len);
    if(text) {
      mask_apply(CUR(a)->mask, text, text_len);
      ui_copy(text);
      free(text);
    }
  }
  if(CUR(a)->pretty_valid && CUR(a)->out_tab == TAB_RESPONSE) {
    struct nk_rect pr;
    float pw = ui_text_w(a->f.small, "Pretty JSON", 11) + 16;
    int hover;
    right -= pw + 4;
    pr = nk_rect(right, r.y + 6, pw, 20);
    hover = ui_hover(a, pr);
    ui_rect(a, pr, 10, CUR(a)->pretty ? p->accent : p->bg);
    if(!CUR(a)->pretty)
      ui_stroke(a, pr, 10, p->border_strong);
    ui_text(a, a->f.small, pr.x + 8, pr.y + (20 - a->f.small->height) / 2, "Pretty JSON", 11,
            CUR(a)->pretty ? p->on_accent : p->dim);
    if(hover)
      a->cursor = CURSOR_HAND;
    if(ui_clicked(a, pr)) {
      CUR(a)->pretty = !CUR(a)->pretty;
      CUR(a)->out_scroll = 0;
    }
  }

  /* status chips, as many as fit */
  {
    struct nk_color fg = p->dim;
    float cy = r.y + 7;
    float limit = right - 4;
#define CHIP(fg_) do { float w_ = ui_pill_w(a, tmp); if(chips_x + w_ <= limit) { ui_pill(a, chips_x, cy, tmp, (fg_), p->bg); chips_x += w_ + 4; } } while(0)
    if(running) {
      snprintf(tmp, sizeof(tmp), "running %.1fs", (double)CUR(a)->run_secs);
      CHIP(p->accent);
    }
    else if(CUR(a)->has_result) {
      if(CUR(a)->has_stats && CUR(a)->http_status) {
        int cls = CUR(a)->http_status / 100;
        fg = cls == 2 ? p->green : (cls == 3 ? p->accent : (cls >= 4 ? p->red : p->dim));
        snprintf(tmp, sizeof(tmp), "%d", CUR(a)->http_status);
        CHIP(fg);
      }
      if(CUR(a)->exit_code) {
        snprintf(tmp, sizeof(tmp), "exit %d", CUR(a)->exit_code);
        CHIP(p->red);
      }
      snprintf(tmp, sizeof(tmp), "%.0f ms", CUR(a)->has_stats ? CUR(a)->curl_time * 1000.0 : (double)CUR(a)->run_secs * 1000.0);
      CHIP(p->dim);
      if(CUR(a)->has_stats) {
        format_size(CUR(a)->curl_size, tmp, sizeof(tmp));
        CHIP(p->dim);
      }
    }
#undef CHIP
  }

  nk_stroke_line(nk_window_get_canvas(a->ctx), r.x, r.y + 32.5f, r.x + r.w, r.y + 32.5f, 1.0f, p->border);
  body = nk_rect(r.x + 1, r.y + 33, r.w - 2, r.h - 34);

  cols = (int)((body.w - 24) / a->f.mono_small_w);
  if(cols < 8)
    cols = 8;
  wrap_update(wc, src, cols);
  content = (float)wc->nrows * ROW_H + 12;

  {
    struct nk_rect saved = g_clip;
    float before = CUR(a)->out_scroll;
    ui_push_clip(a, ui_intersect(body, saved));
    if(ui_wheel(a, body, &CUR(a)->out_scroll, content, ROW_H * 3)) {
      /* scrolling up stops following; reaching the end resumes it */
      if(CUR(a)->out_scroll < before)
        CUR(a)->out_follow = 0;
    }
    if(CUR(a)->out_scroll + body.h >= content - 1)
      CUR(a)->out_follow = 1;
    if(CUR(a)->out_follow && (running || CUR(a)->out_tab == TAB_LOG))
      CUR(a)->out_scroll = content - body.h;
    ui_clamp_scroll(&CUR(a)->out_scroll, content, body.h);

    if(!wc->nrows) {
      if(CUR(a)->url_missing)
        empty_state(a, body, "curl needs a URL to run", "Type it in the bar at the top, then press Run.");
      else if(running)
        empty_state(a, body, "Waiting for the server…", "Output appears here as it arrives.");
      else if(CUR(a)->out_tab == TAB_LOG)
        empty_state(a, body, "No transfer log yet", "Every run records what curl sent and received here.");
      else if(CUR(a)->has_result)
        empty_state(a, body, "The response had no body", "Check the Log tab for the headers and status.");
      else
        empty_state(a, body, "Nothing to show yet", "Enter a URL and press Run, or start from a recipe.");
    }
    else {
      size_t first = (size_t)(CUR(a)->out_scroll / ROW_H), i;
      size_t last = (size_t)((CUR(a)->out_scroll + body.h) / ROW_H) + 1;
      float tx = body.x + 11;
      float base = body.y + 6 - CUR(a)->out_scroll;
      outbuf_lock(src);
      for(i = first; i < wc->nrows && i <= last; i++) {
        const wrap_row *row = &wc->rows[i];
        size_t len;
        const char *line = outbuf_line_locked(src, row->line, &len);
        static char masked[OUTBUF_MAX_LINE + 1];
        float y = base + (float)i * ROW_H + (ROW_H - a->f.mono_small->height) / 2;
        if((size_t)row->off + row->len > len)
          continue;
        /* same length, so the wrapped rows still line up */
        if(CUR(a)->mask && len < sizeof(masked)) {
          memcpy(masked, line, len);
          masked[len] = '\0';
          mask_apply(CUR(a)->mask, masked, len);
          line = masked;
        }
        if(CUR(a)->out_tab == TAB_LOG)
          ui_text(a, a->f.mono_small, tx, y, line + row->off, (int)row->len, log_color(p, line, len));
        else if(showing_pretty && row->line >= CUR(a)->pretty_header_lines)
          draw_json_row(a, tx, y, row, line, len);
        else
          ui_text(a, a->f.mono_small, tx, y, line + row->off, (int)row->len, p->text);
      }
      outbuf_unlock(src);
      if(outbuf_truncated(src))
        ui_text_fit(a, a->f.small, body.x + 11, body.y + body.h - 20, 18, body.w - 22,
                    "Output cut at 8 MB. Use --output to save everything to a file.", p->amber);
    }
    ui_scrollbar(a, body, &CUR(a)->out_scroll, content);
    ui_push_clip(a, saved);
  }
}
