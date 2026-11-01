/* Choosing a file: the same window on every system, so nothing depends on a desktop portal or
   a helper program being there. A field beside each Browse still takes a path by hand. */
#include "app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "fileio.h"

#define ROW_H 26.0f
#define DOUBLE_CLICK_MS 450u

static int enter_dir(app_t *a, const char *dir)
{
  picker_t *k = &a->picker;
  fs_listing fresh;
  char *canon = fs_canon(dir);
  if(!canon)
    return -1;
  if(fs_list(canon, &fresh, k->err, sizeof(k->err))) {
    free(canon);
    return -1;
  }
  fs_listing_free(&k->list);
  k->list = fresh;
  free(k->dir);
  k->dir = canon;
  snprintf(k->path, sizeof(k->path), "%s", canon);
  k->selected = -1;
  k->scroll = 0;
  k->err[0] = '\0';
  k->confirm = 0;
  k->filter[0] = '\0';
  a->pending_unfocus = 1;
  return 0;
}

void picker_open(app_t *a, fs_pick_mode mode, pick_target target, int opt, int index, const char *start)
{
  picker_t *k = &a->picker;
  char *canon = start && start[0] ? fs_canon(start) : NULL;
  int ok = -1;
  picker_close(a);
  k->mode = mode;
  k->target = target;
  k->opt = opt;
  k->index = index;
  k->selected = -1;
  k->last_row = -1;
  k->name[0] = '\0';
  if(canon) {
    int kind = fs_kind(canon);
    if(kind == FS_DIR)
      ok = enter_dir(a, canon);
    else {
      char *up = fs_parent(canon);
      if(up && fs_kind(up) == FS_DIR) {
        ok = enter_dir(a, up);
        if(!ok && mode != FS_PICK_DIR)
          snprintf(k->name, sizeof(k->name), "%s", fs_basename(canon));
      }
      free(up);
    }
    free(canon);
  }
  if(ok && a->set.last_dir[0] && fs_kind(a->set.last_dir) == FS_DIR)
    ok = enter_dir(a, a->set.last_dir);
  if(ok && fs_home())
    ok = enter_dir(a, fs_home());
  if(ok)
    ok = enter_dir(a, ".");
  k->err[0] = ok ? k->err[0] : '\0';
  k->open = 1;
  a->pending_unfocus = 1;
}

void picker_close(app_t *a)
{
  picker_t *k = &a->picker;
  fs_listing_free(&k->list);
  free(k->dir);
  k->dir = NULL;
  k->open = 0;
  k->pending = 0;
  k->confirm = 0;
  k->err[0] = '\0';
  k->filter[0] = '\0';
  a->pending_unfocus = 1;
}

static void deliver(app_t *a, const char *path)
{
  picker_t *k = &a->picker;
  tab_t *t = CUR(a);
  switch(k->target) {
  case PICK_FOR_CARD: {
    int slot = request_find(&t->req, k->opt);
    if(slot >= 0 && k->index < t->req.sel[slot].nvalues) {
      sel_option *s = &t->req.sel[slot];
      char *v = fs_option_value(g_options[k->opt].name, s->values[k->index], path);
      if(v) {
        request_set_value(s, k->index, v);
        free(v);
      }
    }
    break;
  }
  case PICK_FOR_BODY_FILE:
    form_pick_body_file(a, path);
    break;
  case PICK_FOR_FORM_ROW:
    form_pick_row(a, k->index, path);
    break;
  case PICK_FOR_PROGRAM:
    snprintf(a->set.program, sizeof(a->set.program), "%s", path);
    app_detect_curl(a);
    a->doc_built_opt = -2;
    break;
  case PICK_FOR_CONFIG:
    snprintf(a->set.config_file, sizeof(a->set.config_file), "%s", path);
    break;
  case PICK_FOR_IMPORT: {
    size_t len = 0;
    char *text = file_read_all(path, &len, (size_t)16 << 20);
    if(!text)
      snprintf(a->import_err, sizeof(a->import_err), "Cannot read that file");
    else {
      free(a->import_text);
      a->import_text = text;
      a->import_cap = len + 1;
      a->import_err[0] = '\0';
    }
    break;
  }
  }
}

/* OK: goes into a folder, refuses what cannot be, or delivers. Stays open with the reason. */
static void confirm(app_t *a)
{
  picker_t *k = &a->picker;
  char *full, *canon;
  int kind;
  k->err[0] = '\0';
  if(strcmp(k->path, k->dir)) {  /* a folder typed but not yet entered */
    if(fs_kind(k->path) == FS_DIR && !enter_dir(a, k->path) && k->mode != FS_PICK_DIR)
      return;
    if(k->err[0])
      return;
    if(fs_kind(k->path) != FS_DIR) {
      snprintf(k->err, sizeof(k->err), "No such folder");
      return;
    }
  }
  if(k->mode == FS_PICK_DIR) {
    const fs_entry *sel = k->selected >= 0 ? &k->list.e[k->selected] : NULL;
    full = sel && sel->is_dir ? fs_join(k->dir, sel->name) : fs_canon(k->dir);
  }
  else {
    if(!k->name[0]) {
      snprintf(k->err, sizeof(k->err), k->mode == FS_PICK_SAVE ? "Give the file a name" : "Choose a file");
      return;
    }
    full = fs_resolve(k->dir, k->name);
  }
  canon = full ? fs_canon(full) : NULL;
  free(full);
  if(!canon)
    return;
  kind = fs_kind(canon);
  if(k->mode != FS_PICK_DIR && kind == FS_DIR) {
    k->name[0] = '\0';
    enter_dir(a, canon);
    free(canon);
    return;
  }
  if(k->mode == FS_PICK_OPEN && kind != FS_FILE) {
    snprintf(k->err, sizeof(k->err), "No such file: %s", canon);
    free(canon);
    return;
  }
  if(k->mode == FS_PICK_SAVE) {
    char *up = fs_parent(canon);
    if(!up || fs_kind(up) != FS_DIR) {
      snprintf(k->err, sizeof(k->err), "No such folder for this file");
      free(up);
      free(canon);
      return;
    }
    free(up);
    if(kind == FS_FILE && !k->confirm) {
      k->confirm = 1;
      free(canon);
      return;
    }
  }
  if((k->target == PICK_FOR_PROGRAM || k->target == PICK_FOR_CONFIG) && strlen(canon) >= SETTINGS_PATH_CAP) {
    snprintf(k->err, sizeof(k->err), "This path is too long for a setting");
    free(canon);
    return;
  }
  snprintf(a->set.last_dir, sizeof(a->set.last_dir), "%s", strlen(k->dir) < sizeof(a->set.last_dir) ? k->dir : "");
  deliver(a, canon);
  free(canon);
  picker_close(a);
}

void picker_run_pending(app_t *a)
{
  int pending = a->picker.pending;
  a->picker.pending = 0;
  if(!a->picker.open)
    return;
  if(pending == 1)
    confirm(a);
  else if(pending == 2)
    picker_close(a);
}

static void format_size(long long n, char *out, size_t cap)
{
  if(n < 0)
    out[0] = '\0';
  else if(n < 1024)
    snprintf(out, cap, "%lld B", n);
  else if(n < 1048576)
    snprintf(out, cap, "%.1f KB", (double)n / 1024.0);
  else if(n < 1073741824)
    snprintf(out, cap, "%.1f MB", (double)n / 1048576.0);
  else
    snprintf(out, cap, "%.1f GB", (double)n / 1073741824.0);
}

static void format_time(long long t, char *out, size_t cap)
{
  time_t tt = (time_t)t;
  struct tm *tm = t > 0 ? localtime(&tt) : NULL;
  if(!tm || !strftime(out, cap, "%Y-%m-%d %H:%M", tm))
    out[0] = '\0';
}

static void draw_list(app_t *a, struct nk_rect r)
{
  const palette *p = &a->pal;
  picker_t *k = &a->picker;
  struct nk_command_buffer *cv = nk_window_get_canvas(a->ctx);
  float content = 0, y;
  int i, shown = 0;
  for(i = 0; i < k->list.n; i++)
    if((k->show_hidden || !k->list.e[i].hidden) && fs_name_matches(k->list.e[i].name, k->filter))
      shown++;
  content = (float)shown * ROW_H;
  ui_rect(a, r, 5, p->field);
  ui_stroke(a, r, 5, p->border);
  ui_clamp_scroll(&k->scroll, content, r.h);
  ui_wheel(a, r, &k->scroll, content, 52);
  ui_push_clip(a, r);
  y = r.y - k->scroll;
  if(!shown)
    ui_text_fit(a, a->f.small, r.x + 12, r.y + 6, ROW_H, r.w - 24, k->list.n ? "Nothing here matches" : "This folder is empty", p->faint);
  for(i = 0; i < k->list.n; i++) {
    const fs_entry *e = &k->list.e[i];
    struct nk_rect row;
    if((!k->show_hidden && e->hidden) || !fs_name_matches(e->name, k->filter))
      continue;
    row = nk_rect(r.x + 1, y, r.w - 12, ROW_H);
    if(y + ROW_H >= r.y && y <= r.y + r.h) {
      char size[32], when[32];
      int hover = ui_hover(a, row);
      float size_x = r.x + r.w - 250, name_w = size_x - r.x - 40;
      if(i == k->selected)
        ui_rect(a, row, 3, p->accent_soft);
      else if(hover)
        ui_rect(a, row, 3, p->hover);
      /* a folder is a tab and a body; a file is a page */
      if(e->is_dir) {
        nk_fill_rect(cv, nk_rect(row.x + 10, y + 7, 7, 3), 1, p->accent);
        nk_fill_rect(cv, nk_rect(row.x + 10, y + 9, 15, 10), 1, p->accent);
      }
      else
        nk_stroke_rect(cv, nk_rect(row.x + 12, y + 6, 10, 14), 1, 1.0f, p->dim);
      ui_text_fit(a, a->f.sans, row.x + 32, y, ROW_H, name_w, e->name, e->hidden ? p->dim : p->text);
      format_size(e->size, size, sizeof(size));
      format_time(e->mtime, when, sizeof(when));
      ui_text_fit(a, a->f.small, size_x, y, ROW_H, 70, size, p->dim);
      ui_text_fit(a, a->f.small, size_x + 76, y, ROW_H, 150, when, p->dim);
      if(hover)
        a->cursor = CURSOR_HAND;
      if(ui_clicked(a, row)) {
        int twice = k->last_row == i && a->now_ms - k->last_click < DOUBLE_CLICK_MS;
        k->last_row = i;
        k->last_click = a->now_ms;
        k->selected = i;
        k->confirm = 0;
        k->err[0] = '\0';
        if(!e->is_dir)
          snprintf(k->name, sizeof(k->name), "%s", e->name);
        a->pending_unfocus = 1;
        if(twice) {
          k->last_row = -1;
          if(e->is_dir) {
            char *in = fs_join(k->dir, e->name);
            if(in) {
              enter_dir(a, in);
              free(in);
            }
            return;
          }
          k->pending = 1;
        }
      }
    }
    y += ROW_H;
  }
  ui_scrollbar(a, r, &k->scroll, content);
}

void view_picker(app_t *a)
{
  const palette *p = &a->pal;
  picker_t *k = &a->picker;
  float w = NK_MIN(a->W - 80, 760), h = NK_MIN(a->H - 80, 560);
  float x = (a->W - w) / 2, y = (a->H - h) / 2, bx = x + 18, by = y + 46, fw;
  unsigned drives = fs_drives();
  nk_flags st;
  int d;
  char *up = k->dir ? fs_parent(k->dir) : NULL;
  const char *title = k->mode == FS_PICK_SAVE ? "Save as" : k->mode == FS_PICK_DIR ? "Choose a folder" : "Choose a file";
  const char *ok = k->confirm ? "Replace" : k->mode == FS_PICK_SAVE ? "Save" : k->mode == FS_PICK_DIR ? "Use this folder" : "Open";
  float okw = ui_text_w(a->f.semi, ok, (int)strlen(ok)) + 30;

  ui_push_clip(a, nk_rect(0, 0, a->W, a->H));
  ui_rect(a, nk_rect(0, 0, a->W, a->H), 0, nk_rgba(0, 0, 0, 120));
  ui_rect(a, nk_rect(x, y, w, h), 9, p->panel);
  ui_stroke(a, nk_rect(x, y, w, h), 9, p->border_strong);
  ui_text_fit(a, a->f.title, x + 18, y + 12, 26, w - 36, title, p->text);

  /* where we are */
  if(ui_button(a, nk_rect(bx, by, 34, 28), "Up", BTN_SECONDARY) && up)
    enter_dir(a, up);
  bx += 38;
  if(ui_button(a, nk_rect(bx, by, 50, 28), "Home", BTN_SECONDARY) && fs_home())
    enter_dir(a, fs_home());
  bx += 54;
  for(d = 0; d < 26; d++) {
    if(drives & (1u << d)) {
      char label[3] = { (char)('A' + d), ':', '\0' };
      if(ui_button(a, nk_rect(bx, by, 28, 28), label, BTN_SECONDARY)) {
        char root[4] = { (char)('A' + d), ':', '\\', '\0' };
        enter_dir(a, root);
      }
      bx += 30;
    }
  }
  free(up);
  fw = x + w - 18 - bx;
  st = ui_edit(a, nk_rect(bx, by, fw, 28), k->path, (int)sizeof(k->path), "folder", a->f.mono_small, NK_EDIT_SIG_ENTER | NK_EDIT_AUTO_SELECT);
  if(st & NK_EDIT_COMMITED) {
    char *typed = fs_resolve(k->dir, k->path);
    k->err[0] = '\0';
    if(typed && fs_kind(typed) == FS_DIR)
      enter_dir(a, typed);
    else
      snprintf(k->err, sizeof(k->err), "No such folder");
    free(typed);
  }
  by += 34;

  /* what is shown */
  ui_edit(a, nk_rect(x + 18, by, 240, 26), k->filter, (int)sizeof(k->filter), "Filter by name", a->f.small, 0);
  {
    struct nk_rect box = nk_rect(x + 270, by + 5, 16, 16);
    struct nk_rect hit = nk_rect(x + 270, by, 120, 26);
    ui_rect(a, box, 3, k->show_hidden ? p->accent : p->field);
    ui_stroke(a, box, 3, k->show_hidden ? p->accent : p->border_strong);
    ui_text_fit(a, a->f.small, box.x + 22, by, 26, 96, "Hidden files", p->dim);
    if(ui_hover(a, hit))
      a->cursor = CURSOR_HAND;
    if(ui_clicked(a, hit))
      k->show_hidden = !k->show_hidden;
  }
  by += 34;

  draw_list(a, nk_rect(x + 18, by, w - 36, y + h - 96 - by));
  if(!k->open || !k->dir)
    return;
  ui_push_clip(a, nk_rect(0, 0, a->W, a->H));

  by = y + h - 90;
  if(k->mode != FS_PICK_DIR) {
    char before[sizeof(k->name)];
    memcpy(before, k->name, sizeof(before));
    ui_text_fit(a, a->f.small, x + 18, by, 28, 44, "Name", p->dim);
    st = ui_edit(a, nk_rect(x + 62, by, w - 80, 28), k->name, (int)sizeof(k->name), k->mode == FS_PICK_SAVE ? "name of the file" : "name, or a full path",
                 a->f.mono_small, NK_EDIT_SIG_ENTER);
    if(strcmp(before, k->name))
      k->confirm = 0;
    if(st & NK_EDIT_COMMITED)
      k->pending = 1;
  }
  by += 32;
  if(k->err[0])
    ui_text_fit(a, a->f.small, x + 18, by, 20, w - 36, k->err, p->red);
  else if(k->confirm)
    ui_text_fit(a, a->f.small, x + 18, by, 20, w - 36, "This file exists. curl may overwrite it when the request runs.", p->amber);

  by = y + h - 40;
  if(ui_button(a, nk_rect(x + w - 18 - okw, by, okw, 28), ok, BTN_PRIMARY))
    k->pending = 1;
  if(ui_button(a, nk_rect(x + w - 18 - okw - 8 - 72, by, 72, 28), "Cancel", BTN_SECONDARY))
    k->pending = 2;
}
