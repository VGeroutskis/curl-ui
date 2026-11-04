/* Saving: what open tabs and the tree of collections do with the core's files. */
#include "app.h"
#include "oas_import.h"
#include "cmdparse.h"
#include "fileio.h"
#include "reqjson.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SESSION_MAX ((size_t)1 << 30)

const char *tab_coll(const tab_t *t)
{
  return t->coll_id[0] ? t->coll_id : VARS_DEFAULT_ID;
}

var_store *app_tab_store(app_t *a, const tab_t *t)
{
  return book_get(&a->book, tab_coll(t));
}

var_env *app_tab_env(app_t *a, const tab_t *t)
{
  var_store *st = app_tab_store(a, t);
  return &st->e[st->active];
}

void app_open_vars(app_t *a, const char *coll_id)
{
  snprintf(a->vars_for, sizeof(a->vars_for), "%s", coll_id);
  a->show_vars = 1;
  a->show_settings = 0;
  a->env_edit_for = -1;
  a->env_del_armed = 0;
  a->pending_unfocus = 1;
}

/* A request now in another collection takes along the variables it uses. */
static void tab_follow_collection(app_t *a, tab_t *t, const coll_node *n)
{
  const collection *c = coll_of(a->colls, a->ncolls, n);
  const char *id = c ? c->id : VARS_DEFAULT_ID;
  if(strcmp(id, tab_coll(t))) {
    var_env *from = app_tab_env(a, t);
    var_store *to = book_get(&a->book, id);
    if(to)
      vars_copy_used(from, &to->e[to->active], &t->args_tmpl);
  }
  snprintf(t->coll_id, sizeof(t->coll_id), "%s", id);
}

void app_tabs_refresh_coll(app_t *a)
{
  int i;
  for(i = 0; i < a->ntabs; i++) {
    tab_t *t = a->tabs[i];
    const coll_node *n = t->node_id[0] ? app_find_node(a, t->node_id) : NULL;
    if(n)
      tab_follow_collection(a, t, n);
  }
}

void app_ensure_default_collection(app_t *a)
{
  collection *c;
  int i;
  for(i = 0; i < a->ncolls; i++)
    if(!strcmp(a->colls[i]->id, VARS_DEFAULT_ID))
      return;
  c = app_new_collection(a, "My requests");
  if(c) {
    /* first in the tree, and on disk only when something goes in it */
    memcpy(c->id, VARS_DEFAULT_ID, sizeof(c->id));
    memmove(&a->colls[1], &a->colls[0], sizeof(collection *) * (size_t)(a->ncolls - 1));
    a->colls[0] = c;
  }
}

static const char *const g_method_names[METHOD_COUNT] = {
  "", "GET", "POST", "PUT", "PATCH", "DELETE", "HEAD", "OPTIONS"
};

static jv *tab_to_jv(tab_t *t)
{
  int i;
  kv_free(&t->hints.disabled_params);
  for(i = 0; i < t->params.n; i++)
    if(!t->params.r[i].enabled)
      kv_add(&t->hints.disabled_params, t->params.r[i].key, t->params.r[i].value, 0);
  kv_free(&t->hints.disabled_body);
  for(i = 0; i < t->body_rows.n; i++) {
    if(!t->body_rows.r[i].enabled) {
      kv_row *off = kv_add(&t->hints.disabled_body, t->body_rows.r[i].key, t->body_rows.r[i].value, 0);
      if(off)
        off->is_file = t->body_rows.r[i].is_file;
    }
  }
  return req_to_jv(&t->req, g_method_names[t->method], &t->hints, t->extra);
}

static char *tab_serial(tab_t *t)
{
  jv *v = tab_to_jv(t);
  char *s = jv_write(v, 0, NULL);
  jv_free(v);
  return s;
}

void tab_mark_clean(tab_t *t)
{
  free(t->saved_json);
  t->saved_json = tab_serial(t);
  t->dirty = 0;
  t->dirty_sig = 0;
}

/* Rows switched off are saved and are nowhere in the command. */
static unsigned long rows_sig(unsigned long h, const kv_list *l, int only_off)
{
  int i;
  const char *s;
  for(i = 0; i < l->n; i++) {
    if(only_off && l->r[i].enabled)
      continue;
    for(s = l->r[i].key; *s; s++)
      h = (h ^ (unsigned char)*s) * 16777619u;
    h = (h ^ 0xFF) * 16777619u;
    for(s = l->r[i].value; *s; s++)
      h = (h ^ (unsigned char)*s) * 16777619u;
    h = (h ^ 0xFE) * 16777619u;
  }
  return h;
}

static unsigned long tab_sig(const tab_t *t)
{
  unsigned long h = form_request_sig(&t->req);
  int i;
  h = (h ^ (unsigned long)t->method) * 16777619u;
  for(i = 0; i < t->params.n; i++)
    h = (h ^ (unsigned long)t->params.r[i].enabled) * 16777619u;
  h = rows_sig(h, &t->params, 1);
  h = rows_sig(h, &t->hints.disabled_headers, 0);
  h = rows_sig(h, &t->body_rows, 1);
  h = rows_sig(h, &t->header_rows, 1);
  return h + 1;
}

void app_tabs_update_dirty(app_t *a)
{
  int i;
  for(i = 0; i < a->ntabs; i++) {
    tab_t *t = a->tabs[i];
    unsigned long sig = tab_sig(t);
    if(sig != t->dirty_sig) {
      char *now = tab_serial(t);
      t->dirty = !now || !t->saved_json || strcmp(now, t->saved_json) != 0;
      free(now);
      t->dirty_sig = sig;
    }
  }
}

/* Fills t, whose request is empty, from saved JSON. */
static void tab_load(tab_t *t, const jv *req)
{
  char method[16];
  int i;
  form_hints_free(&t->hints);
  form_hints_init(&t->hints);
  jv_free(t->extra);
  t->extra = NULL;
  req_from_jv(req, &t->req, method, sizeof(method), &t->hints, &t->extra);
  t->method = 0;
  for(i = 1; i < METHOD_COUNT; i++)
    if(!strcmp(g_method_names[i], method))
      t->method = i;
  form_params_sync(t);
  for(i = 0; i < t->hints.disabled_params.n; i++)
    kv_add(&t->params, t->hints.disabled_params.r[i].key, t->hints.disabled_params.r[i].value, 0);
  kv_free(&t->body_rows);
  for(i = 0; i < t->hints.disabled_body.n; i++) {
    kv_row *off = kv_add(&t->body_rows, t->hints.disabled_body.r[i].key, t->hints.disabled_body.r[i].value, 0);
    if(off)
      off->is_file = t->hints.disabled_body.r[i].is_file;
  }
}

static int tab_is_blank(const tab_t *t)
{
  return !t->req.url[0] && !t->req.nsel && !t->node_id[0] && !t->proc && !t->has_result;
}

/* A tab for a saved request: the one it is already in, or a new one. */
void app_open_node(app_t *a, coll_node *n)
{
  int i;
  tab_t *t;
  if(n->kind != NODE_REQUEST)
    return;
  for(i = 0; i < a->ntabs; i++) {
    if(!strcmp(a->tabs[i]->node_id, n->id)) {
      a->cur = i;
      a->show_vars = a->show_settings = 0;
      a->pending_unfocus = 1;
      return;
    }
  }
  if(!tab_is_blank(CUR(a)) && tab_new(a) < 0) {
    app_notice(a, "Too many tabs are open.");
    return;
  }
  t = CUR(a);
  tab_load(t, n->req);
  memcpy(t->node_id, n->id, sizeof(t->node_id));
  snprintf(t->title, sizeof(t->title), "%s", n->name);
  {
    const collection *c = coll_of(a->colls, a->ncolls, n);
    snprintf(t->coll_id, sizeof(t->coll_id), "%s", c ? c->id : "");
  }
  tab_mark_clean(t);
  if(t->extra)
    app_notice(a, "This request has options this version does not know. They are kept when you save it.");
  a->show_vars = a->show_settings = 0;
  a->pending_unfocus = 1;
}

coll_node *app_find_node(app_t *a, const char *id)
{
  int i;
  for(i = 0; i < a->ncolls; i++) {
    coll_node *n = coll_find(a->colls[i]->root, id);
    if(n)
      return n;
  }
  return NULL;
}

int app_save_coll_of(app_t *a, const coll_node *n)
{
  collection *c = coll_of(a->colls, a->ncolls, n);
  if(!c || coll_save(c, a->coll_dir)) {
    app_notice(a, c && c->read_only ? "This collection comes from a newer version, and is only shown here."
                                    : "Could not save the collection.");
    return -1;
  }
  return 0;
}

collection *app_new_collection(app_t *a, const char *name)
{
  collection *c = coll_new(name);
  collection **g;
  if(!c)
    return NULL;
  g = realloc(a->colls, sizeof(collection *) * (size_t)(a->ncolls + 1));
  if(!g) {
    coll_free(c);
    return NULL;
  }
  a->colls = g;
  a->colls[a->ncolls++] = c;
  return c;
}

/* Saves the tab into its node. Returns 0, or -1 with a notice and the tab still dirty. */
int tab_save_to_node(app_t *a, tab_t *t, coll_node *n)
{
  jv *old = n->req;
  n->req = tab_to_jv(t);
  if(app_save_coll_of(a, n)) {
    jv_free(n->req);
    n->req = old;
    return -1;
  }
  jv_free(old);
  tab_follow_collection(a, t, n);
  memcpy(t->node_id, n->id, sizeof(t->node_id));
  snprintf(t->title, sizeof(t->title), "%s", n->name);
  tab_mark_clean(t);
  return 0;
}

/* Ctrl+S: into its own node, or ask where. */
void app_save_current(app_t *a)
{
  tab_t *t = CUR(a);
  coll_node *n = t->node_id[0] ? app_find_node(a, t->node_id) : NULL;
  if(n)
    tab_save_to_node(a, t, n);
  else
    dialog_open(a, DLG_SAVE_AS, NULL);
}

static coll_node *child_named(coll_node *parent, const char *name, size_t n)
{
  int i;
  for(i = 0; i < parent->nkids; i++)
    if(parent->kids[i]->kind == NODE_FOLDER && strlen(parent->kids[i]->name) == n &&
       !memcmp(parent->kids[i]->name, name, n))
      return parent->kids[i];
  return NULL;
}

/* COLLECTION/FOLDER/.../NAME, creating what is not there. For scripts. */
int app_save_as_path(app_t *a, const char *path)
{
  const char *p = strchr(path, '/');
  const char *last = strrchr(path, '/');
  collection *c = NULL;
  coll_node *at;
  char first[256];
  int i;
  coll_node *n;
  if(!p)
    return -1;
  snprintf(first, sizeof(first), "%.*s", (int)(p - path), path);
  for(i = 0; i < a->ncolls && !c; i++)
    if(!strcmp(a->colls[i]->root->name, first))
      c = a->colls[i];
  if(!c)
    c = app_new_collection(a, first);
  if(!c)
    return -1;
  at = c->root;
  p++;
  while(p <= last) {
    const char *slash = strchr(p, '/');
    size_t len = (size_t)(slash - p);
    coll_node *f = child_named(at, p, len);
    if(!f) {
      char name[256];
      snprintf(name, sizeof(name), "%.*s", (int)len, p);
      f = coll_add(at, NODE_FOLDER, name, NULL);
      if(!f)
        return -1;
    }
    at = f;
    p = slash + 1;
  }
  n = coll_add(at, NODE_REQUEST, last + 1, NULL);
  if(!n)
    return -1;
  if(tab_save_to_node(a, CUR(a), n)) {
    coll_remove(n);
    return -1;
  }
  return 0;
}

/* ---- history ---- */

void app_history_begin(app_t *a, tab_t *t)
{
  jv_free(t->run_req);
  t->run_req = tab_to_jv(t);
  snprintf(t->run_method, sizeof(t->run_method), "%s", app_method_label(a, t, t->method));
}

void app_history_end(app_t *a, tab_t *t)
{
  hist_entry e;
  size_t n;
  const char *url = jv_str(jv_get(t->run_req, "url"));
  if(!t->run_req)
    return;
  memset(&e, 0, sizeof(e));
  e.when = (long long)time(NULL);
  e.status = t->http_status;
  e.secs = t->has_stats ? t->curl_time : (double)t->run_secs;
  e.size = t->has_stats ? t->curl_size : 0;
  n = strlen(t->run_method) + 1;
  e.method = malloc(n);
  if(e.method)
    memcpy(e.method, t->run_method, n);
  n = strlen(url ? url : "") + 1;
  e.url = malloc(n);
  if(e.url)
    memcpy(e.url, url ? url : "", n);
  e.req = t->run_req;
  t->run_req = NULL;
  if(!e.method || !e.url) {
    free(e.method);
    free(e.url);
    jv_free(e.req);
    return;
  }
  if(a->hist_path[0])
    history_append(&a->hist, a->hist_path, &e);
  else {
    /* a scripted run with nowhere to keep it: nothing lasts, and nothing should */
    free(e.method);
    free(e.url);
    jv_free(e.req);
  }
}

void app_open_history(app_t *a, int index)
{
  tab_t *t;
  if(index < 0 || index >= a->hist.n)
    return;
  if(!tab_is_blank(CUR(a)) && tab_new(a) < 0) {
    app_notice(a, "Too many tabs are open.");
    return;
  }
  t = CUR(a);
  tab_load(t, a->hist.e[index].req);
  a->show_vars = a->show_settings = 0;
  a->pending_unfocus = 1;
}

int app_import_command(app_t *a, const char *text, char *err, size_t errn)
{
  request r;
  char method[16];
  tab_t *t;
  int i;
  request_init(&r);
  if(cmd_parse(text, &r, method, sizeof(method), err, errn)) {
    request_free(&r);
    return -1;
  }
  if(!tab_is_blank(CUR(a)) && tab_new(a) < 0) {
    request_free(&r);
    snprintf(err, errn, "Too many tabs are open");
    return -1;
  }
  t = CUR(a);
  request_free(&t->req);
  t->req = r;
  t->method = 0;
  for(i = 1; i < METHOD_COUNT; i++)
    if(!strcmp(g_method_names[i], method))
      t->method = i;
  t->form = FORM_CURL;
  a->show_vars = a->show_settings = 0;
  a->pending_unfocus = 1;
  return 0;
}

int app_import_openapi(app_t *a, const char *path, char *err, size_t errn)
{
  const char **taken = a->ncolls ? malloc(sizeof(char *) * (size_t)a->ncolls) : NULL;
  collection *c = NULL, **g;
  var_store vars, *st;
  oas_report rep;
  char text[2048];
  int i, rc, warns;
  for(i = 0; taken && i < a->ncolls; i++)
    taken[i] = a->colls[i]->root->name;
  rc = oas_import_file(path, taken, taken ? a->ncolls : 0, &c, &vars, &rep, err, errn);
  free(taken);
  if(rc) {
    oas_report_free(&rep);
    return -1;
  }
  g = realloc(a->colls, sizeof(collection *) * (size_t)(a->ncolls + 1));
  st = g ? book_get(&a->book, c->id) : NULL;
  if(g)
    a->colls = g;
  if(!st) {
    snprintf(err, errn, "out of memory");
    coll_free(c);
    vars_free(&vars);
    oas_report_free(&rep);
    return -1;
  }
  a->colls[a->ncolls++] = c;
  vars_free(st);
  *st = vars;
  c->root->open = 1;
  oas_report_text(&rep, c->root->name, text, sizeof(text));
  warns = oas_report_warns(&rep);
  oas_report_free(&rep);
  if(a->coll_dir[0] && coll_save(c, a->coll_dir)) {
    snprintf(text + strlen(text), sizeof(text) - strlen(text), " But the collection could not be saved.");
    warns = 1;
  }
  app_vars_save(a);
  if(warns)
    app_notice(a, text);
  else
    app_notice_info(a, text);
  a->sidebar = SIDE_COLLECTIONS;
  a->show_vars = a->show_settings = 0;
  a->pending_unfocus = 1;
  return 0;
}

/* ---- at start and at exit ---- */

void app_store_load(app_t *a)
{
  char err[4096];
  err[0] = '\0';
  if(a->hist_path[0])
    history_load(&a->hist, a->hist_path);
  if(!a->coll_dir[0])
    return;
  coll_load_dir(a->coll_dir, &a->colls, &a->ncolls, err, sizeof(err));
  if(err[0])
    app_notice(a, err);
}

void app_session_load(app_t *a)
{
  size_t len = 0;
  char *text;
  jv *root, *tabs;
  int i, opened = 0;
  if(!a->session_path[0])
    return;
  text = file_read_all(a->session_path, &len, SESSION_MAX);
  if(!text)
    return;
  root = jv_parse(text, len, NULL, 0);
  free(text);
  if(!root) {
    char moved[2100], msg[2200];
    if(file_quarantine(a->session_path, moved, sizeof(moved)))
      snprintf(moved, sizeof(moved), "%s", a->session_path);
    snprintf(msg, sizeof(msg), "The open tabs could not be restored. The file was kept as %s", moved);
    app_notice(a, msg);
    return;
  }
  tabs = jv_get(root, "tabs");
  for(i = 0; i < jv_len(tabs); i++) {
    const jv *row = jv_at(tabs, i);
    const char *node = jv_str(jv_get(row, "node"));
    const coll_node *n = node ? app_find_node(a, node) : NULL;
    tab_t *t;
    if(opened && tab_new(a) < 0)
      break;
    t = CUR(a);
    opened = 1;
    if(n) {
      /* what it was saved as first, to tell what the tab has changed since */
      tab_load(t, n->req);
      tab_mark_clean(t);
      request_free(&t->req);
      request_init(&t->req);
      kv_free(&t->params);
      free(t->params_url);
      t->params_url = NULL;
      memcpy(t->node_id, n->id, sizeof(t->node_id));
      snprintf(t->title, sizeof(t->title), "%s", n->name);
      {
        const collection *c = coll_of(a->colls, a->ncolls, n);
        snprintf(t->coll_id, sizeof(t->coll_id), "%s", c ? c->id : "");
      }
    }
    else
      tab_mark_clean(t);
    tab_load(t, jv_get(row, "request"));
  }
  if(opened) {
    int cur = (int)jv_num(jv_get(root, "cur"));
    a->cur = cur >= 0 && cur < a->ntabs ? cur : 0;
  }
  jv_free(root);
}

void app_session_save(app_t *a)
{
  jv *root, *tabs;
  char *text;
  size_t n = 0;
  int i;
  if(!a->session_path[0])
    return;
  if(!a->set.reopen_tabs) {
    remove(a->session_path);
    return;
  }
  root = jv_new(JV_OBJ);
  tabs = jv_new(JV_ARR);
  jv_set(root, "format", jv_new_str("curl-ui-session"));
  jv_set(root, "version", jv_new_num(1));
  jv_set(root, "cur", jv_new_num(a->cur));
  for(i = 0; i < a->ntabs; i++) {
    jv *row = jv_new(JV_OBJ);
    jv_set(row, "request", tab_to_jv(a->tabs[i]));
    if(a->tabs[i]->node_id[0])
      jv_set(row, "node", jv_new_str(a->tabs[i]->node_id));
    jv_push(tabs, row);
  }
  jv_set(root, "tabs", tabs);
  text = jv_write(root, 1, &n);
  jv_free(root);
  if(text) {
    file_write_private(a->session_path, text, n);
    free(text);
  }
}

void app_store_free(app_t *a)
{
  int i;
  for(i = 0; i < a->ncolls; i++)
    coll_free(a->colls[i]);
  free(a->colls);
  a->colls = NULL;
  a->ncolls = 0;
  free(a->dlg_targets);
  a->dlg_targets = NULL;
  history_free(&a->hist);
  picker_close(a);
  str_list_free(&a->panel_names);
  free(a->import_text);
  a->import_text = NULL;
}
