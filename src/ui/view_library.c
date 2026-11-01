#include "app.h"

#include <stdio.h>
#include <string.h>

typedef struct {
  const char *label, *filter;
} chip;

/* The first 13 are always visible; the rest sit behind "More". */
static const chip chips[] = {
  { "All", OPT_FILTER_ALL }, { "Popular", OPT_FILTER_POPULAR }, { "HTTP", "http" },
  { "POST", "post" }, { "Auth", "auth" }, { "TLS", "tls" }, { "Proxy", "proxy" },
  { "Output", "output" }, { "Connection", "connection" }, { "Timeouts", "timeout" },
  { "Debug", "verbose" }, { "Upload", "upload" }, { "DNS", "dns" },
  { "FTP", "ftp" }, { "SFTP", "sftp" }, { "SCP", "scp" }, { "SSH", "ssh" },
  { "SMTP", "smtp" }, { "IMAP", "imap" }, { "POP3", "pop3" }, { "LDAP", "ldap" },
  { "File", "file" }, { "TFTP", "tftp" }, { "MQTT", "mqtt" }, { "Telnet", "telnet" },
  { "curl", "curl" }, { "Global", "global" }, { "Deprecated", "deprecated" },
};
#define CHIP_COUNT ((int)(sizeof(chips) / sizeof(chips[0])))
#define CHIPS_VISIBLE 13
#define ROW_H 50.0f

static float chip_w(app_t *a, const char *label)
{
  return ui_text_w(a->f.small, label, (int)strlen(label)) + 16;
}

static int draw_chip(app_t *a, float x, float y, const char *label, int active)
{
  const palette *p = &a->pal;
  struct nk_rect r = nk_rect(x, y, chip_w(a, label), 22);
  int hover = ui_hover(a, r);
  ui_rect(a, r, 11, active ? p->accent : (hover ? p->hover : p->panel));
  if(!active)
    ui_stroke(a, r, 11, hover ? p->border_strong : p->border);
  ui_text(a, a->f.small, x + 8, y + (22 - a->f.small->height) * 0.5f, label, (int)strlen(label),
          active ? p->on_accent : p->dim);
  if(hover)
    a->cursor = CURSOR_HAND;
  return ui_clicked(a, r);
}

/* First protocol, plus how many more. */
static void protocol_label(const opt_def *o, char *out, size_t n)
{
  const char *sp = strchr(o->protocols, ' ');
  if(!o->protocols[0]) {
    out[0] = '\0';
    return;
  }
  if(sp) {
    int more = 1;
    const char *q = sp + 1;
    while((q = strchr(q, ' ')) != NULL) {
      more++;
      q++;
    }
    snprintf(out, n, "%.*s +%d", (int)(sp - o->protocols), o->protocols, more);
  }
  else
    snprintf(out, n, "%s", o->protocols);
}

static void draw_row(app_t *a, struct nk_rect r, int opt)
{
  const palette *p = &a->pal;
  const opt_def *o = &g_options[opt];
  int added = request_find(&a->req, opt) >= 0;
  int hover = ui_hover(a, r);
  char flag[128], proto[64];
  float x = r.x + 14, right = r.x + r.w - 16, fw, y1 = r.y + 7;
  float pillw = 0;

  if(added)
    ui_rect(a, r, 0, p->accent_soft);
  else if(hover)
    ui_rect(a, r, 0, p->hover);
  if(added)
    ui_rect(a, nk_rect(r.x, r.y, 3, r.h), 0, p->accent);
  nk_stroke_line(nk_window_get_canvas(a->ctx), r.x, r.y + r.h - 0.5f, r.x + r.w, r.y + r.h - 0.5f, 1.0f, p->border);

  if(hover) {
    const char *label = added ? "Remove" : "+ Add";
    pillw = ui_pill_w(a, label);
    ui_pill(a, right - pillw, y1, label, added ? p->red : p->on_accent, added ? p->card : p->accent);
  }
  else {
    protocol_label(o, proto, sizeof(proto));
    if(proto[0]) {
      pillw = ui_pill_w(a, proto);
      ui_pill(a, right - pillw, y1, proto, p->dim, p->bg);
    }
  }

  if(o->short_flag[0])
    snprintf(flag, sizeof(flag), "-%s, --%s", o->short_flag, o->name);
  else
    snprintf(flag, sizeof(flag), "--%s", o->name);
  fw = ui_text_w(a->f.mono, flag, (int)strlen(flag));
  ui_text_fit(a, a->f.mono, x, y1, 18, right - pillw - 8 - x, flag, o->deprecated ? p->faint : p->accent);
  if(o->arg[0] && x + fw + 8 < right - pillw - 8)
    ui_text_fit(a, a->f.mono_small, x + fw + 6, y1, 18, right - pillw - 8 - (x + fw + 6), o->arg, p->faint);
  ui_text_fit(a, a->f.small, x, y1 + 19, 16, r.w - 30, o->help, p->dim);

  if(hover) {
    a->cursor = CURSOR_HAND;
    ui_hover_doc(a, opt);
    if(ui_clicked(a, r))
      app_push(a, ACT_TOGGLE, opt, 0);
  }
}

void view_library(app_t *a, struct nk_rect r)
{
  const palette *p = &a->pal;
  float x = r.x + 12, y = r.y + 12, w = r.w - 24, cx;
  int i, shown;
  char count[64];
  struct nk_rect list;
  float content;
  int first, last;

  ui_push_clip(a, r);
  ui_rect(a, r, 0, p->panel);
  nk_stroke_line(nk_window_get_canvas(a->ctx), r.x + r.w - 0.5f, r.y, r.x + r.w - 0.5f, r.y + r.h, 1.0f, p->border);

  ui_edit(a, nk_rect(x, y, w, 32), a->search, SEARCH_CAP, "Search 278 options by name or description", a->f.sans, 0);
  y += 40;

  cx = x;
  shown = a->show_more ? CHIP_COUNT : CHIPS_VISIBLE;
  for(i = 0; i <= shown; i++) {
    int is_more = i == shown;
    const char *label = is_more ? (a->show_more ? "Less" : "More") : chips[i].label;
    float cw = chip_w(a, label);
    if(cx + cw > x + w) {
      cx = x;
      y += 26;
    }
    if(draw_chip(a, cx, y, label, !is_more && a->filter == i)) {
      if(is_more) {
        a->show_more = !a->show_more;
        if(!a->show_more && a->filter >= CHIPS_VISIBLE)
          a->filter = 0;
      }
      else
        a->filter = i;
    }
    cx += cw + 4;
  }
  y += 30;

  if(!a->results_valid || a->filter != a->filter_last || strcmp(a->search, a->search_last)) {
    a->nresults = options_search(a->search, chips[a->filter].filter, a->results);
    a->filter_last = a->filter;
    snprintf(a->search_last, sizeof(a->search_last), "%s", a->search);
    a->results_valid = 1;
    a->lib_scroll = 0;
  }
  snprintf(count, sizeof(count), "%d of %d options", a->nresults, g_option_count);
  ui_text_fit(a, a->f.small, x, y, 16, w, count, p->faint);
  y += 20;
  nk_stroke_line(nk_window_get_canvas(a->ctx), r.x, y - 0.5f, r.x + r.w - 1, y - 0.5f, 1.0f, p->border);

  list = nk_rect(r.x, y, r.w - 1, r.y + r.h - y);
  content = (float)a->nresults * ROW_H;
  ui_clamp_scroll(&a->lib_scroll, content, list.h);
  ui_wheel(a, list, &a->lib_scroll, content, ROW_H * 1.5f);

  ui_push_clip(a, list);
  first = (int)(a->lib_scroll / ROW_H);
  last = (int)((a->lib_scroll + list.h) / ROW_H);
  if(last >= a->nresults)
    last = a->nresults - 1;
  for(i = first; i <= last; i++)
    draw_row(a, nk_rect(list.x, list.y + (float)i * ROW_H - a->lib_scroll, list.w, ROW_H), a->results[i]);
  if(!a->nresults) {
    ui_text_fit(a, a->f.sans, x, list.y + 16, 18, w, "No option matches this search.", p->dim);
    ui_text_fit(a, a->f.small, x, list.y + 36, 16, w, "Try fewer words, or the All chip.", p->faint);
  }
  ui_scrollbar(a, list, &a->lib_scroll, content);
}
