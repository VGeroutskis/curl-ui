#include "app.h"

#include <stdio.h>
#include <string.h>

#define ROW_H 44.0f
#define LABEL_W 176.0f

/* Segmented choice. Returns the index clicked, or -1. */
static int segment(app_t *a, struct nk_rect r, const char *const *labels, int n, int selected)
{
  const palette *p = &a->pal;
  const struct nk_user_font *f = a->f.small;
  float w = r.w / (float)n;
  int i, clicked = -1;
  ui_rect(a, r, 5, p->bg);
  for(i = 0; i < n; i++) {
    struct nk_rect c = nk_rect(r.x + (float)i * w, r.y, w, r.h);
    int hover = ui_hover(a, c);
    float tw = ui_text_w(f, labels[i], (int)strlen(labels[i]));
    if(i == selected)
      ui_rect(a, c, 5, p->accent);
    else if(hover)
      ui_rect(a, c, 5, p->hover);
    ui_text(a, f, c.x + (c.w - tw) / 2, c.y + (c.h - f->height) / 2, labels[i], (int)strlen(labels[i]),
            i == selected ? p->on_accent : p->dim);
    if(hover)
      a->cursor = CURSOR_HAND;
    if(ui_clicked(a, c))
      clicked = i;
  }
  ui_stroke(a, r, 5, p->border);
  return clicked;
}

static void row_label(app_t *a, float x, float y, const char *title, const char *hint, float w)
{
  const palette *p = &a->pal;
  if(hint) {
    ui_text_fit(a, a->f.sans, x, y + 2, 20, LABEL_W - 10, title, p->text);
    ui_text_fit(a, a->f.small, x, y + 22, 16, w, hint, p->faint);
  }
  else
    ui_text_fit(a, a->f.sans, x, y, 30, LABEL_W - 10, title, p->text);
}

/* A row with an on/off switch: the whole row is the target. */
static void toggle_row(app_t *a, float x, float y, float w, const char *title, const char *hint, int *value)
{
  const palette *p = &a->pal;
  struct nk_rect row = nk_rect(x, y, w, ROW_H - 4);
  struct nk_rect track = nk_rect(x + w - 38, y + 8, 38, 20);
  int hover = ui_hover(a, row);
  float knob = *value ? track.x + track.w - 18 : track.x + 2;
  ui_text_fit(a, a->f.sans, x, y + 2, 20, w - 50, title, p->text);
  ui_text_fit(a, a->f.small, x, y + 22, 16, w - 50, hint, p->faint);
  ui_rect(a, track, 10, *value ? p->accent : p->border_strong);
  nk_fill_circle(nk_window_get_canvas(a->ctx), nk_rect(knob, track.y + 2, 16, 16), *value ? p->on_accent : p->panel);
  if(hover)
    a->cursor = CURSOR_HAND;
  if(ui_clicked(a, row))
    *value = !*value;
}

static float heading(app_t *a, float x, float y, float w, const char *label)
{
  const palette *p = &a->pal;
  ui_text_fit(a, a->f.semi, x, y, 22, w, label, p->accent);
  nk_stroke_line(nk_window_get_canvas(a->ctx), x, y + 25.5f, x + w, y + 25.5f, 1.0f, p->border);
  return y + 34;
}

void view_settings(app_t *a, struct nk_rect r)
{
  static const char *const themes[] = { "Dark", "Light" };
  static const char *const sizes[] = { "Auto", "100%", "125%", "150%", "175%", "200%" };
  static const int size_values[] = { 0, 100, 125, 150, 175, 200 };
  static const char *const hovers[] = { "Instant", "Normal", "Slow" };
  static const int hover_values[] = { 0, 110, 300 };
  const palette *p = &a->pal;
  settings_t *set = &a->set;
  struct nk_rect body;
  float x, w, y, field_x, field_w;
  float content = 590;
  int i, sel;
  char tmp[SETTINGS_PATH_CAP + 64];

  ui_push_clip(a, r);
  ui_text_fit(a, a->f.title, r.x + 16, r.y + 12, 28, 200, "Settings", p->text);
  if(ui_button(a, nk_rect(r.x + r.w - 16 - 70, r.y + 12, 70, 28), "Done", BTN_PRIMARY)) {
    a->show_settings = 0;
    a->pending_unfocus = 1;
  }

  body = nk_rect(r.x, r.y + 48, r.w, r.h - 48);
  ui_clamp_scroll(&a->settings_scroll, content, body.h);
  /* edits are identified by draw order: hold still while one has the focus */
  if(!a->settings_focus)
    ui_wheel(a, body, &a->settings_scroll, content, 50);
  ui_clamp_scroll(&a->settings_scroll, content, body.h);
  a->settings_focus = 0;
  ui_push_clip(a, body);

  x = r.x + 16;
  w = r.w - 32 - 8;
  field_x = x + LABEL_W;
  field_w = w - LABEL_W;
  y = body.y + 4 - a->settings_scroll;

  y = heading(a, x, y, w, "Appearance");
  row_label(a, x, y, "Theme", NULL, w);
  i = segment(a, nk_rect(field_x, y + 2, NK_MIN(field_w, 180), 28), themes, 2, set->dark ? 0 : 1);
  if(i >= 0)
    theme_apply(a, i == 0);
  y += ROW_H - 6;
  row_label(a, x, y, "Text size", NULL, w);
  sel = -1;  /* a size set in the file may be none of these */
  for(i = 0; i < 6; i++)
    if(size_values[i] == set->zoom)
      sel = i;
  i = segment(a, nk_rect(field_x, y + 2, field_w, 28), sizes, 6, sel);
  if(i >= 0)
    set->zoom = size_values[i];
  y += ROW_H + 2;

  y = heading(a, x, y, w, "curl");
  row_label(a, x, y, "Program", NULL, w);
  {
    struct nk_rect detect = nk_rect(x + w - 74, y + 1, 74, 28);
    nk_flags st = ui_edit(a, nk_rect(field_x, y + 1, field_w - 80, 28), set->program, (int)sizeof(set->program), "curl",
                          a->f.mono_small, 0);
    if(st & NK_EDIT_ACTIVE)
      a->settings_focus = 1;
    if(ui_button(a, detect, "Detect", BTN_SECONDARY)) {
      app_detect_curl(a);
      a->doc_built_opt = -2;
    }
  }
  y += 32;
  ui_text_fit(a, a->f.small, field_x, y, 18, field_w, a->curl.found ? a->curl.banner :
              "That program did not answer --version like curl does.", a->curl.found ? p->dim : p->red);
  y += 26;
  row_label(a, x, y, "Your config file", NULL, w);
  {
    nk_flags st = ui_edit(a, nk_rect(field_x, y + 1, field_w, 28), set->config_file, (int)sizeof(set->config_file),
                          "/full/path/to/my-curl.conf, passed as --config", a->f.mono_small, 0);
    if(st & NK_EDIT_ACTIVE)
      a->settings_focus = 1;
  }
  y += ROW_H - 8;
  toggle_row(a, x, y, w, "Ignore ~/.curlrc", "Starts every command with --disable", &set->no_curlrc);
  y += ROW_H + 2;

  y = heading(a, x, y, w, "Behaviour");
  toggle_row(a, x, y, w, "Pretty JSON by default", "Format valid JSON responses", &set->pretty);
  y += ROW_H;
  toggle_row(a, x, y, w, "Fill the Log tab and status chips", "Adds --verbose, --no-progress-meter, --write-out to runs",
             &set->capture);
  y += ROW_H;
  toggle_row(a, x, y, w, "Remember the last request", "Saved in a private file, including any passwords in it",
             &set->remember);
  y += ROW_H;
  row_label(a, x, y, "Docs hover delay", NULL, w);
  sel = -1;
  for(i = 0; i < 3; i++)
    if(hover_values[i] == set->hover_ms)
      sel = i;
  i = segment(a, nk_rect(field_x, y + 2, NK_MIN(field_w, 240), 28), hovers, 3, sel);
  if(i >= 0)
    set->hover_ms = hover_values[i];
  y += ROW_H;

  if(a->settings_path[0]) {
    snprintf(tmp, sizeof(tmp), "Saved automatically in %s", a->settings_path);
    ui_text_fit(a, a->f.small, x, y, 18, w, tmp, p->faint);
  }
  else
    ui_text_fit(a, a->f.small, x, y, 18, w, "Not saved: this session has no settings file.", p->amber);
  y += 24;
  if(ui_button(a, nk_rect(x, y, 130, 26), "Reset to defaults", BTN_SECONDARY))
    a->reset_settings = 1;

  ui_scrollbar(a, body, &a->settings_scroll, content);
}
