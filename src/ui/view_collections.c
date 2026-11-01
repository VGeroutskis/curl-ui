/* The left column's views, the tree of collections, and the dialogs that go with them. */
#include "app.h"
#include "reqjson.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define NODE_H 26.0f
#define STRIP_H 34.0f

/* ---- the tree ---- */

static int contains_ci(const char *hay, const char *needle)
{
  size_t n = strlen(needle);
  if(!n)
    return 1;
  for(; *hay; hay++) {
    size_t i = 0;
    while(i < n && hay[i] && tolower((unsigned char)hay[i]) == tolower((unsigned char)needle[i]))
      i++;
    if(i == n)
      return 1;
  }
  return 0;
}

static int matches(const coll_node *n, const char *q)
{
  int i;
  if(contains_ci(n->name, q))
    return 1;
  for(i = 0; i < n->nkids; i++)
    if(matches(n->kids[i], q))
      return 1;
  return 0;
}

static void draw_node(app_t *a, struct nk_rect r, coll_node *n, int depth, float *y, const char *q, int top)
{
  const palette *p = &a->pal;
  struct nk_command_buffer *cv = nk_window_get_canvas(a->ctx);
  struct nk_rect row;
  float x = r.x + 8 + (float)depth * 14;
  int i, hover, open_kids;
  if(q[0] && !matches(n, q))
    return;
  row = nk_rect(r.x, *y, r.w, NODE_H);
  hover = ui_hover(a, row);
  if(hover)
    ui_rect(a, row, 0, p->hover);
  if(n->kind == NODE_FOLDER) {
    float cy = *y + NODE_H / 2;
    if(n->open || q[0])
      nk_fill_triangle(cv, x, cy - 3, x + 8, cy - 3, x + 4, cy + 3, p->dim);
    else
      nk_fill_triangle(cv, x + 1, cy - 4, x + 1, cy + 4, x + 7, cy, p->dim);
    ui_text_fit(a, top ? a->f.semi : a->f.small, x + 13, *y, NODE_H, r.w - (x - r.x) - 44, n->name, p->text);
  }
  else {
    int active = !strcmp(CUR(a)->node_id, n->id);
    ui_text_fit(a, a->f.small, x + 13, *y, NODE_H, r.w - (x - r.x) - 44, n->name, active ? p->accent : p->text);
  }
  if(hover) {
    struct nk_rect dots = nk_rect(r.x + r.w - 28, *y + 3, 22, 20);
    int hd = ui_hover(a, dots);
    float cx = dots.x + 11, cy = dots.y + 10;
    nk_fill_circle(cv, nk_rect(cx - 7, cy - 1.5f, 3, 3), hd ? p->text : p->dim);
    nk_fill_circle(cv, nk_rect(cx - 1.5f, cy - 1.5f, 3, 3), hd ? p->text : p->dim);
    nk_fill_circle(cv, nk_rect(cx + 4, cy - 1.5f, 3, 3), hd ? p->text : p->dim);
    a->cursor = CURSOR_HAND;
    if(hd) {
      if(ui_clicked(a, dots)) {
        a->tree_menu = n;
        a->tree_menu_at = nk_vec2(dots.x, dots.y + dots.h);
      }
    }
    else if(ui_clicked(a, row)) {
      if(n->kind == NODE_FOLDER)
        n->open = !n->open;
      else
        app_open_node(a, n);
    }
  }
  *y += NODE_H;
  open_kids = n->kind == NODE_FOLDER && (n->open || q[0]);
  for(i = 0; open_kids && i < n->nkids; i++)
    draw_node(a, r, n->kids[i], depth + 1, y, q, 0);
}

static float tree_height(const coll_node *n, const char *q)
{
  float h;
  int i;
  if(q[0] && !matches(n, q))
    return 0;
  h = NODE_H;
  if(n->kind == NODE_FOLDER && (n->open || q[0]))
    for(i = 0; i < n->nkids; i++)
      h += tree_height(n->kids[i], q);
  return h;
}

static void view_collections(app_t *a, struct nk_rect r)
{
  const palette *p = &a->pal;
  float x = r.x + 12, w = r.w - 24, y = r.y;
  struct nk_rect body;
  float content = 0, ty;
  int i;
  ui_edit(a, nk_rect(x, y, w, 32), a->coll_search, (int)sizeof(a->coll_search), "Search collections", a->f.sans, 0);
  y += 38;
  if(ui_button(a, nk_rect(x, y, w, 26), "+ New collection", BTN_SECONDARY))
    dialog_open(a, DLG_NEW_COLL, NULL);
  y += 32;
  body = nk_rect(r.x, y, r.w, r.y + r.h - y);
  for(i = 0; i < a->ncolls; i++)
    content += tree_height(a->colls[i]->root, a->coll_search);
  ui_clamp_scroll(&a->tree_scroll, content, body.h);
  ui_wheel(a, body, &a->tree_scroll, content, 40);
  ui_clamp_scroll(&a->tree_scroll, content, body.h);
  ui_push_clip(a, body);
  if(!a->ncolls) {
    ui_text_fit(a, a->f.small, x, y, 22, w, "No collections yet.", p->dim);
    ui_text_fit(a, a->f.small, x, y + 20, 22, w, "Ctrl+S saves the request in a tab.", p->faint);
    return;
  }
  ty = y - a->tree_scroll;
  for(i = 0; i < a->ncolls; i++)
    draw_node(a, body, a->colls[i]->root, 0, &ty, a->coll_search, 1);
  ui_scrollbar(a, body, &a->tree_scroll, content);
}

#define HIST_ROW 44.0f

void view_history(app_t *a, struct nk_rect r)
{
  const palette *p = &a->pal;
  float x = r.x + 12, w = r.w - 24, y = r.y, content, ty;
  struct nk_rect body;
  int i;
  if(!a->hist.n) {
    ui_text_fit(a, a->f.small, x, y, 22, w, "Nothing has run yet.", p->dim);
    return;
  }
  if(ui_button(a, nk_rect(x, y, w, 26), a->hist_clear_armed ? "Really clear history?" : "Clear history",
               a->hist_clear_armed ? BTN_DANGER : BTN_SECONDARY)) {
    if(a->hist_clear_armed) {
      history_clear(&a->hist, a->hist_path);
      a->hist_clear_armed = 0;
      return;
    }
    a->hist_clear_armed = 1;
  }
  y += 32;
  body = nk_rect(r.x, y, r.w, r.y + r.h - y);
  content = (float)a->hist.n * HIST_ROW;
  ui_clamp_scroll(&a->hist_scroll, content, body.h);
  ui_wheel(a, body, &a->hist_scroll, content, 40);
  ui_clamp_scroll(&a->hist_scroll, content, body.h);
  ui_push_clip(a, body);
  ty = y - a->hist_scroll;
  for(i = a->hist.n - 1; i >= 0; i--) {
    const hist_entry *e = &a->hist.e[i];
    struct nk_rect row = nk_rect(r.x, ty, r.w, HIST_ROW);
    if(ty + HIST_ROW >= body.y && ty <= body.y + body.h) {
      int hover = ui_hover(a, row);
      char line[64], stamp[32], status[16];
      time_t when = (time_t)e->when;
      struct tm *tm = localtime(&when);
      float mw;
      if(hover)
        ui_rect(a, row, 0, p->hover);
      mw = ui_text_w(a->f.mono_semi, e->method, (int)strlen(e->method)) + 8;
      ui_text_fit(a, a->f.mono_semi, x, ty + 4, 18, mw, e->method, p->accent);
      ui_text_fit(a, a->f.mono_small, x + mw, ty + 4, 18, w - mw, e->url, p->text);
      if(tm)
        strftime(stamp, sizeof(stamp), "%d %b %H:%M", tm);
      else
        stamp[0] = '\0';
      if(e->status)
        snprintf(status, sizeof(status), "%d", e->status);
      else
        snprintf(status, sizeof(status), "failed");
      snprintf(line, sizeof(line), "%s  %.2f s  %lld B  %s", status, e->secs, e->size, stamp);
      ui_text_fit(a, a->f.small, x, ty + 22, 18, w, line, e->status && e->status < 400 ? p->dim : p->amber);
      if(hover) {
        a->cursor = CURSOR_HAND;
        if(ui_clicked(a, row)) {
          app_open_history(a, i);
          return;
        }
      }
    }
    ty += HIST_ROW;
  }
  ui_scrollbar(a, body, &a->hist_scroll, content);
}

void view_sidebar(app_t *a, struct nk_rect r)
{
  static const char *const names[SIDE_COUNT] = { "Collections", "History", "Options" };
  const palette *p = &a->pal;
  struct nk_command_buffer *cv = nk_window_get_canvas(a->ctx);
  float w = r.w / SIDE_COUNT;
  struct nk_rect below = nk_rect(r.x, r.y + STRIP_H, r.w, r.h - STRIP_H);
  int i;
  ui_push_clip(a, r);
  ui_rect(a, r, 0, p->panel);
  nk_stroke_line(cv, r.x + r.w - 0.5f, r.y, r.x + r.w - 0.5f, r.y + r.h, 1.0f, p->border);
  for(i = 0; i < SIDE_COUNT; i++) {
    struct nk_rect c = nk_rect(r.x + (float)i * w, r.y, w, STRIP_H);
    int active = a->sidebar == (side_kind)i, hover = ui_hover(a, c);
    float tw = ui_text_w(a->f.small, names[i], (int)strlen(names[i]));
    ui_text(a, a->f.small, c.x + (c.w - tw) / 2, c.y + (c.h - a->f.small->height) / 2, names[i], (int)strlen(names[i]),
            active || hover ? p->text : p->dim);
    if(active)
      ui_rect(a, nk_rect(c.x + 8, c.y + c.h - 2, c.w - 16, 2), 1, p->accent);
    if(hover)
      a->cursor = CURSOR_HAND;
    if(ui_clicked(a, c) && !active) {
      a->sidebar = (side_kind)i;
      a->pending_unfocus = 1;
    }
  }
  if(a->sidebar == SIDE_OPTIONS) {
    view_library(a, below);
    return;
  }
  below.y += 6;
  below.h -= 6;
  if(a->sidebar == SIDE_COLLECTIONS)
    view_collections(a, below);
  else
    view_history(a, below);
}

/* ---- the node's menu ---- */

typedef enum { TM_NEW_REQ, TM_NEW_FOLDER, TM_RENAME, TM_COPY, TM_MOVE, TM_DELETE } tm_item;

static int tree_menu_items(const coll_node *n, tm_item *out)
{
  int c = 0;
  if(n->kind == NODE_FOLDER) {
    out[c++] = TM_NEW_REQ;
    out[c++] = TM_NEW_FOLDER;
  }
  out[c++] = TM_RENAME;
  if(n->parent) {
    out[c++] = TM_COPY;
    out[c++] = TM_MOVE;
  }
  out[c++] = TM_DELETE;
  return c;
}

static struct nk_rect tree_menu_rect(app_t *a)
{
  tm_item items[6];
  int n = tree_menu_items(a->tree_menu, items);
  return nk_rect(a->tree_menu_at.x, a->tree_menu_at.y, 130, (float)n * 26 + 8);
}

void tree_menu_click(app_t *a, struct nk_vec2 pos)
{
  static const tm_item none = (tm_item)-1;
  tm_item items[6], hit = none;
  coll_node *n = a->tree_menu;
  struct nk_rect m = tree_menu_rect(a);
  int c = tree_menu_items(n, items), i;
  for(i = 0; i < c; i++)
    if(NK_INBOX(pos.x, pos.y, m.x + 4, m.y + 4 + (float)i * 26, m.w - 8, 26))
      hit = items[i];
  a->tree_menu = NULL;
  switch(hit) {
  case TM_NEW_REQ:
  case TM_NEW_FOLDER:
    dialog_open(a, hit == TM_NEW_REQ ? DLG_NEW_REQUEST : DLG_NEW_FOLDER, n);
    break;
  case TM_RENAME:
    dialog_open(a, DLG_RENAME, n);
    break;
  case TM_COPY: {
    coll_node *copy = coll_copy(n);
    if(copy) {
      char name[300];
      snprintf(name, sizeof(name), "%.280s copy", n->name);
      coll_rename(copy, name);
      app_save_coll_of(a, n);
    }
    break;
  }
  case TM_MOVE:
    dialog_open(a, DLG_MOVE, n);
    break;
  case TM_DELETE:
    dialog_open(a, DLG_DELETE, n);
    break;
  default:
    break;
  }
}

void tree_menu_draw(app_t *a)
{
  static const char *const labels[] = { "New request", "New folder", "Rename", "Duplicate", "Move...", "Delete" };
  const palette *p = &a->pal;
  const struct nk_input *in = &a->ctx->input;
  tm_item items[6];
  struct nk_rect m = tree_menu_rect(a);
  int c = tree_menu_items(a->tree_menu, items), i;
  ui_push_clip(a, nk_rect(0, 0, a->W, a->H));
  ui_rect(a, nk_rect(m.x + 1, m.y + 2, m.w, m.h), 7, nk_rgba(0, 0, 0, 60));
  ui_rect(a, m, 7, p->card);
  ui_stroke(a, m, 7, p->border_strong);
  for(i = 0; i < c; i++) {
    struct nk_rect item = nk_rect(m.x + 4, m.y + 4 + (float)i * 26, m.w - 8, 26);
    if(nk_input_is_mouse_hovering_rect(in, item)) {
      ui_rect(a, item, 4, p->hover);
      a->cursor = CURSOR_HAND;
    }
    ui_text_fit(a, a->f.small, item.x + 8, item.y, 26, item.w - 16, labels[items[i]], items[i] == TM_DELETE ? p->red : p->text);
  }
}

/* ---- dialogs ---- */

static void add_target(app_t *a, coll_node *n, const coll_node *except)
{
  int i;
  coll_node **g;
  collection *c = coll_of(a->colls, a->ncolls, n);
  if(n == except || n->kind != NODE_FOLDER || (c && c->read_only))
    return;
  g = realloc(a->dlg_targets, sizeof(coll_node *) * (size_t)(a->dlg_ntargets + 1));
  if(!g)
    return;
  a->dlg_targets = g;
  a->dlg_targets[a->dlg_ntargets++] = n;
  for(i = 0; i < n->nkids; i++)
    add_target(a, n->kids[i], except);
}

void dialog_open(app_t *a, dialog_kind kind, coll_node *node)
{
  int i;
  a->dlg = kind;
  a->dlg_node = node;
  a->dlg_ntargets = 0;
  a->dlg_target = 0;
  a->dlg_scroll = 0;
  a->dlg_name[0] = '\0';
  a->import_err[0] = '\0';
  a->pending_unfocus = 1;
  if(kind == DLG_IMPORT && !a->import_text) {
    a->import_cap = 4096;
    a->import_text = calloc(1, a->import_cap);
  }
  if(kind == DLG_RENAME)
    snprintf(a->dlg_name, sizeof(a->dlg_name), "%s", node->name);
  if(kind == DLG_SAVE_AS) {
    const tab_t *t = CUR(a);
    snprintf(a->dlg_name, sizeof(a->dlg_name), "%s", t->title[0] ? t->title : "New request");
  }
  if(kind == DLG_SAVE_AS || kind == DLG_MOVE)
    for(i = 0; i < a->ncolls; i++)
      add_target(a, a->colls[i]->root, kind == DLG_MOVE ? node : NULL);
}

static void dialog_close(app_t *a)
{
  a->dlg = DLG_NONE;
  a->dlg_node = NULL;
  a->pending_unfocus = 1;
}

static void forget_node_tabs(app_t *a, const coll_node *n)
{
  int i;
  if(n->kind == NODE_REQUEST) {
    for(i = 0; i < a->ntabs; i++) {
      if(!strcmp(a->tabs[i]->node_id, n->id)) {
        /* still open, now unsaved */
        a->tabs[i]->node_id[0] = '\0';
        a->tabs[i]->dirty_sig = 0;
        free(a->tabs[i]->saved_json);
        a->tabs[i]->saved_json = NULL;
      }
    }
  }
  for(i = 0; i < n->nkids; i++)
    forget_node_tabs(a, n->kids[i]);
}

static void dialog_confirm(app_t *a)
{
  coll_node *n = a->dlg_node;
  switch(a->dlg) {
  case DLG_NEW_COLL:
    if(a->dlg_name[0]) {
      collection *c = app_new_collection(a, a->dlg_name);
      if(c && coll_save(c, a->coll_dir))
        app_notice(a, "Could not save the collection.");
    }
    break;
  case DLG_NEW_FOLDER:
    if(a->dlg_name[0] && coll_add(n, NODE_FOLDER, a->dlg_name, NULL)) {
      n->open = 1;
      app_save_coll_of(a, n);
    }
    break;
  case DLG_NEW_REQUEST:
    if(a->dlg_name[0]) {
      request r;
      form_hints h;
      jv *v;
      coll_node *added;
      request_init(&r);
      form_hints_init(&h);
      v = req_to_jv(&r, "", &h, NULL);
      added = coll_add(n, NODE_REQUEST, a->dlg_name, v);
      request_free(&r);
      form_hints_free(&h);
      if(added) {
        n->open = 1;
        app_save_coll_of(a, n);
        app_open_node(a, added);
      }
    }
    break;
  case DLG_RENAME:
    if(a->dlg_name[0]) {
      int i;
      coll_rename(n, a->dlg_name);
      for(i = 0; i < a->ntabs; i++)
        if(!strcmp(a->tabs[i]->node_id, n->id))
          snprintf(a->tabs[i]->title, sizeof(a->tabs[i]->title), "%s", n->name);
      app_save_coll_of(a, n);
    }
    break;
  case DLG_MOVE:
    if(a->dlg_target < a->dlg_ntargets) {
      collection *from = coll_of(a->colls, a->ncolls, n);
      coll_node *to = a->dlg_targets[a->dlg_target];
      if(!coll_move(n, to)) {
        collection *dest = coll_of(a->colls, a->ncolls, to);
        to->open = 1;
        if(from && coll_save(from, a->coll_dir))
          app_notice(a, "Could not save the collection.");
        if(dest && dest != from && coll_save(dest, a->coll_dir))
          app_notice(a, "Could not save the collection.");
      }
      else
        app_notice(a, "A folder cannot move into itself.");
    }
    break;
  case DLG_DELETE: {
    collection *c = coll_of(a->colls, a->ncolls, n);
    forget_node_tabs(a, n);
    if(n->parent) {
      coll_node *keep = n->parent;
      coll_remove(n);
      app_save_coll_of(a, keep);
    }
    else if(c) {
      int i;
      coll_delete_file(c, a->coll_dir);
      for(i = 0; i < a->ncolls; i++) {
        if(a->colls[i] == c) {
          memmove(&a->colls[i], &a->colls[i + 1], sizeof(collection *) * (size_t)(a->ncolls - i - 1));
          a->ncolls--;
          break;
        }
      }
      coll_free(c);
    }
    break;
  }
  case DLG_IMPORT:
    a->import_err[0] = '\0';
    if(app_import_command(a, a->import_text ? a->import_text : "", a->import_err, sizeof(a->import_err)))
      return;  /* stays open with the reason */
    a->import_text[0] = '\0';
    break;
  case DLG_SAVE_AS: {
    coll_node *to = a->dlg_target < a->dlg_ntargets ? a->dlg_targets[a->dlg_target] : NULL;
    if(!to) {
      collection *c = app_new_collection(a, "My requests");
      to = c ? c->root : NULL;
    }
    if(to && a->dlg_name[0]) {
      coll_node *added = coll_add(to, NODE_REQUEST, a->dlg_name, NULL);
      if(added && !tab_save_to_node(a, CUR(a), added)) {
        to->open = 1;
        if(a->dlg_close_after)
          tab_close(a, a->cur);
      }
      else if(added)
        coll_remove(added);
    }
    a->dlg_close_after = 0;
    break;
  }
  default:
    break;
  }
  dialog_close(a);
}

/* Closing a tab with unsaved changes asks first. */
void app_request_close(app_t *a, int i)
{
  if(i >= 0 && i < a->ntabs && a->tabs[i]->dirty) {
    a->cur = i;
    dialog_open(a, DLG_CLOSE_TAB, NULL);
  }
  else
    tab_close(a, i);
}

static void list_targets(app_t *a, float x, float y, float w, float h)
{
  const palette *p = &a->pal;
  struct nk_rect box = nk_rect(x, y, w, h);
  float content = (float)a->dlg_ntargets * 26;
  float ty;
  int i;
  ui_stroke(a, box, 5, p->border);
  ui_clamp_scroll(&a->dlg_scroll, content, h);
  ui_wheel(a, box, &a->dlg_scroll, content, 40);
  ui_clamp_scroll(&a->dlg_scroll, content, h);
  ui_push_clip(a, box);
  ty = y - a->dlg_scroll;
  for(i = 0; i < a->dlg_ntargets; i++) {
    struct nk_rect row = nk_rect(x, ty, w, 26);
    const coll_node *n = a->dlg_targets[i], *q;
    int depth = 0, hover = ui_hover(a, row);
    for(q = n->parent; q; q = q->parent)
      depth++;
    if(i == a->dlg_target)
      ui_rect(a, row, 0, p->accent_soft);
    else if(hover)
      ui_rect(a, row, 0, p->hover);
    ui_text_fit(a, a->f.small, x + 10 + (float)depth * 14, ty, 26, w - 20, n->name, i == a->dlg_target ? p->accent : p->text);
    if(hover)
      a->cursor = CURSOR_HAND;
    if(ui_clicked(a, row))
      a->dlg_target = i;
    ty += 26;
  }
  ui_push_clip(a, nk_rect(0, 0, a->W, a->H));
}

void view_dialog(app_t *a)
{
  const palette *p = &a->pal;
  float w = NK_MIN(480, a->W - 80), h = 150, x, y, by;
  const char *title = "", *ok = "OK", *hint = NULL;
  int name_field = 1, targets = 0, destructive = 0;
  char tmp[400];
  switch(a->dlg) {
  case DLG_NEW_COLL: title = "New collection"; ok = "Create"; break;
  case DLG_NEW_FOLDER: title = "New folder"; ok = "Create"; break;
  case DLG_NEW_REQUEST: title = "New request"; ok = "Create"; break;
  case DLG_RENAME: title = "Rename"; ok = "Rename"; break;
  case DLG_SAVE_AS: title = "Save request"; ok = "Save"; targets = a->dlg_ntargets > 0; break;
  case DLG_MOVE: title = "Move to"; ok = "Move"; name_field = 0; targets = 1; break;
  case DLG_DELETE:
    snprintf(tmp, sizeof(tmp), "Delete %.300s?", a->dlg_node->name);
    title = tmp;
    ok = "Delete";
    name_field = 0;
    destructive = 1;
    hint = a->dlg_node->kind == NODE_REQUEST ? "If it is open in a tab, the tab stays, unsaved." : "With everything in it.";
    break;
  case DLG_CLOSE_TAB:
    title = "This tab has unsaved changes";
    ok = "Save";
    name_field = 0;
    break;
  case DLG_IMPORT:
    title = "Import a curl command";
    ok = "Import";
    name_field = 0;
    hint = "Paste the command. Nothing runs until you press Run.";
    h += 200;
    break;
  case DLG_QUIT:
    title = "Some tabs have unsaved changes";
    ok = "Quit anyway";
    name_field = 0;
    destructive = 1;
    hint = "Reopen tabs is off, so they will be lost.";
    break;
  default:
    return;
  }
  if(targets)
    h += 170;
  if(!name_field)
    h -= 40;
  x = (a->W - w) / 2;
  y = (a->H - h) / 2;
  ui_push_clip(a, nk_rect(0, 0, a->W, a->H));
  ui_rect(a, nk_rect(0, 0, a->W, a->H), 0, nk_rgba(0, 0, 0, 120));
  ui_rect(a, nk_rect(x, y, w, h), 9, p->panel);
  ui_stroke(a, nk_rect(x, y, w, h), 9, p->border_strong);
  ui_text_fit(a, a->f.title, x + 18, y + 12, 26, w - 36, title, p->text);
  by = y + 46;
  if(hint)
    ui_text_fit(a, a->f.small, x + 18, by, 20, w - 36, hint, p->dim);
  if(name_field) {
    nk_flags st = ui_edit(a, nk_rect(x + 18, by, w - 36, 30), a->dlg_name, (int)sizeof(a->dlg_name), "name", a->f.sans,
                          NK_EDIT_SIG_ENTER);
    if(st & NK_EDIT_COMMITED)
      a->dlg_pending = 1;
    by += 38;
  }
  if(a->dlg == DLG_IMPORT) {
    struct nk_rect box = nk_rect(x + 18, by + 24, w - 36, 186);
    ui_edit_dyn(a, box, &a->import_text, &a->import_cap, "curl https://example.com -H 'Accept: application/json'",
                a->f.mono_small, NK_EDIT_MULTILINE);
    if(a->import_err[0])
      ui_text_fit(a, a->f.small, x + 18, y + h - 66, 20, w - 36, a->import_err, p->red);
  }
  if(targets) {
    list_targets(a, x + 18, by, w - 36, 160);
    by += 168;
  }
  by = y + h - 40;
  {
    float okw = ui_text_w(a->f.sans, ok, (int)strlen(ok)) + 26;
    if(ui_button(a, nk_rect(x + w - 18 - okw, by, okw, 28), ok, destructive ? BTN_DANGER : BTN_PRIMARY))
      a->dlg_pending = 1;
    if(ui_button(a, nk_rect(x + w - 18 - okw - 8 - 72, by, 72, 28), "Cancel", BTN_SECONDARY))
      a->dlg_pending = 2;
    if(a->dlg == DLG_CLOSE_TAB && ui_button(a, nk_rect(x + 18, by, 84, 28), "Discard", BTN_DANGER))
      a->dlg_pending = 3;
  }
}

void dialog_run_pending(app_t *a)
{
  int act = a->dlg_pending;
  a->dlg_pending = 0;
  if(!act)
    return;
  if(act == 2) {
    a->dlg_close_after = 0;
    dialog_close(a);
    return;
  }
  if(a->dlg == DLG_CLOSE_TAB) {
    if(act == 3) {
      dialog_close(a);
      tab_close(a, a->cur);
    }
    else {
      coll_node *n = CUR(a)->node_id[0] ? app_find_node(a, CUR(a)->node_id) : NULL;
      dialog_close(a);
      if(n) {
        if(!tab_save_to_node(a, CUR(a), n))
          tab_close(a, a->cur);
      }
      else {
        a->dlg_close_after = 1;
        dialog_open(a, DLG_SAVE_AS, NULL);
      }
    }
    return;
  }
  if(a->dlg == DLG_QUIT) {
    a->quit = 1;
    dialog_close(a);
    return;
  }
  dialog_confirm(a);
}
