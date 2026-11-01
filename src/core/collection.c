#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "collection.h"
#include "fileio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <unistd.h>
#endif

#define COLL_FILE_MAX ((size_t)1 << 30)

void coll_make_id(char out[17])
{
  static unsigned long long counter;
  unsigned long long v;
  long long t = (long long)time(NULL);
#ifdef _WIN32
  unsigned long long pid = (unsigned long long)GetCurrentProcessId();
#else
  unsigned long long pid = (unsigned long long)getpid();
#endif
  v = ((unsigned long long)t << 22) ^ (pid << 44) ^ (++counter * 0x9E3779B97F4A7C15ULL);
  snprintf(out, 17, "%016llx", v);
}

static char *dup_str(const char *s)
{
  size_t n = strlen(s) + 1;
  char *d = malloc(n);
  if(d)
    memcpy(d, s, n);
  return d;
}

static coll_node *node_new(node_kind k, const char *name, jv *req)
{
  coll_node *n = calloc(1, sizeof(*n));
  if(!n) {
    jv_free(req);
    return NULL;
  }
  n->kind = k;
  n->name = dup_str(name);
  n->req = req;
  coll_make_id(n->id);
  return n;
}

static void node_free(coll_node *n)
{
  int i;
  if(!n)
    return;
  for(i = 0; i < n->nkids; i++)
    node_free(n->kids[i]);
  free(n->kids);
  free(n->name);
  jv_free(n->req);
  free(n);
}

static int attach(coll_node *parent, coll_node *n)
{
  if(parent->nkids == parent->capkids) {
    int cap = parent->capkids ? parent->capkids * 2 : 8;
    coll_node **g = realloc(parent->kids, sizeof(coll_node *) * (size_t)cap);
    if(!g)
      return -1;
    parent->kids = g;
    parent->capkids = cap;
  }
  parent->kids[parent->nkids++] = n;
  n->parent = parent;
  return 0;
}

static void detach(coll_node *n)
{
  coll_node *p = n->parent;
  int i;
  if(!p)
    return;
  for(i = 0; i < p->nkids; i++) {
    if(p->kids[i] == n) {
      memmove(&p->kids[i], &p->kids[i + 1], sizeof(coll_node *) * (size_t)(p->nkids - i - 1));
      p->nkids--;
      break;
    }
  }
  n->parent = NULL;
}

collection *coll_new(const char *name)
{
  collection *c = calloc(1, sizeof(*c));
  if(!c)
    return NULL;
  coll_make_id(c->id);
  c->root = node_new(NODE_FOLDER, name, NULL);
  if(c->root)
    c->root->open = 1;
  return c;
}

void coll_free(collection *c)
{
  if(!c)
    return;
  node_free(c->root);
  free(c);
}

coll_node *coll_add(coll_node *parent, node_kind k, const char *name, jv *req)
{
  coll_node *n = node_new(k, name, req);
  if(n && attach(parent, n)) {
    node_free(n);
    return NULL;
  }
  return n;
}

void coll_remove(coll_node *n)
{
  detach(n);
  node_free(n);
}

static coll_node *clone_tree(const coll_node *n)
{
  coll_node *c = node_new(n->kind, n->name, jv_clone(n->req));
  int i;
  if(!c)
    return NULL;
  c->open = n->open;
  for(i = 0; i < n->nkids; i++) {
    coll_node *k = clone_tree(n->kids[i]);
    if(k)
      attach(c, k);
  }
  return c;
}

coll_node *coll_copy(const coll_node *n)
{
  coll_node *c;
  if(!n->parent)
    return NULL;
  c = clone_tree(n);
  if(c)
    attach(n->parent, c);
  return c;
}

int coll_move(coll_node *n, coll_node *new_parent)
{
  const coll_node *p;
  if(new_parent->kind != NODE_FOLDER || !n->parent)
    return -1;
  for(p = new_parent; p; p = p->parent)
    if(p == n)
      return -1;
  detach(n);
  return attach(new_parent, n);
}

int coll_rename(coll_node *n, const char *name)
{
  char *d = dup_str(name);
  if(!d)
    return -1;
  free(n->name);
  n->name = d;
  return 0;
}

coll_node *coll_find(coll_node *root, const char *id)
{
  int i;
  if(!strcmp(root->id, id))
    return root;
  for(i = 0; i < root->nkids; i++) {
    coll_node *f = coll_find(root->kids[i], id);
    if(f)
      return f;
  }
  return NULL;
}

collection *coll_of(collection **list, int n, const coll_node *node)
{
  const coll_node *top = node;
  int i;
  while(top && top->parent)
    top = top->parent;
  for(i = 0; i < n; i++)
    if(list[i]->root == top)
      return list[i];
  return NULL;
}

/* ---- file ---- */

static jv *node_to_jv(const coll_node *n)
{
  jv *o = jv_new(JV_OBJ);
  int i;
  jv_set(o, "type", jv_new_str(n->kind == NODE_FOLDER ? "folder" : "request"));
  jv_set(o, "id", jv_new_str(n->id));
  jv_set(o, "name", jv_new_str(n->name));
  if(n->kind == NODE_REQUEST)
    jv_set(o, "request", jv_clone(n->req));
  else {
    jv *items = jv_new(JV_ARR);
    for(i = 0; i < n->nkids; i++)
      jv_push(items, node_to_jv(n->kids[i]));
    jv_set(o, "items", items);
  }
  return o;
}

int coll_save(const collection *c, const char *dir)
{
  jv *o, *items;
  char *text, *path;
  size_t n = 0, pn = strlen(dir) + 32;
  int i, rc;
  if(c->read_only)
    return -1;
  o = jv_new(JV_OBJ);
  items = jv_new(JV_ARR);
  jv_set(o, "format", jv_new_str("curl-ui-collection"));
  jv_set(o, "version", jv_new_num(COLL_VERSION));
  jv_set(o, "id", jv_new_str(c->id));
  jv_set(o, "name", jv_new_str(c->root->name));
  for(i = 0; i < c->root->nkids; i++)
    jv_push(items, node_to_jv(c->root->kids[i]));
  jv_set(o, "items", items);
  text = jv_write(o, 1, &n);
  jv_free(o);
  path = malloc(pn);
  if(!text || !path) {
    free(text);
    free(path);
    return -1;
  }
  snprintf(path, pn, "%s/%s.json", dir, c->id);
  rc = file_write_private(path, text, n);
  free(text);
  free(path);
  return rc;
}

void coll_delete_file(const collection *c, const char *dir)
{
  char path[2048];
  snprintf(path, sizeof(path), "%s/%s.json", dir, c->id);
  remove(path);
}

static int id_ok(const char *s)
{
  size_t i;
  if(!s || strlen(s) != 16)
    return 0;
  for(i = 0; i < 16; i++)
    if(!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f')))
      return 0;
  return 1;
}

static void nodes_from_jv(const jv *items, coll_node *parent)
{
  int i;
  for(i = 0; i < jv_len(items); i++) {
    const jv *it = jv_at(items, i);
    const char *type = jv_str(jv_get(it, "type"));
    const char *name = jv_str(jv_get(it, "name"));
    const char *id = jv_str(jv_get(it, "id"));
    int folder = type && !strcmp(type, "folder");
    coll_node *n;
    if(!name || !type)
      continue;
    n = coll_add(parent, folder ? NODE_FOLDER : NODE_REQUEST, name, folder ? NULL : jv_clone(jv_get(it, "request")));
    if(!n)
      continue;
    if(id_ok(id))
      memcpy(n->id, id, 17);
    if(folder)
      nodes_from_jv(jv_get(it, "items"), n);
  }
}

static void note_err(char *err, size_t errn, const char *text)
{
  size_t used = strlen(err);
  if(used + 2 < errn)
    snprintf(err + used, errn - used, "%s%s", used ? "; " : "", text);
}

static void load_one(const char *path, collection ***out, int *n, char *err, size_t errn)
{
  size_t len = 0;
  char *text = file_read_all(path, &len, COLL_FILE_MAX);
  char perr[128];
  jv *root;
  const char *format, *name;
  collection *c, **grown;
  if(!text)
    return;
  perr[0] = '\0';
  root = jv_parse(text, len, perr, sizeof(perr));
  free(text);
  format = jv_str(jv_get(root, "format"));
  name = jv_str(jv_get(root, "name"));
  if(!root || !format || strcmp(format, "curl-ui-collection") || !name) {
    char moved[2200], msg[2400];
    jv_free(root);
    if(file_quarantine(path, moved, sizeof(moved)))
      snprintf(moved, sizeof(moved), "%s", path);
    snprintf(msg, sizeof(msg), "A collection could not be read and was kept as %s", moved);
    note_err(err, errn, msg);
    return;
  }
  c = coll_new(name);
  if(c) {
    const char *id = jv_str(jv_get(root, "id"));
    if(id_ok(id))
      memcpy(c->id, id, 17);
    c->read_only = jv_num(jv_get(root, "version")) > COLL_VERSION;
    nodes_from_jv(jv_get(root, "items"), c->root);
    grown = realloc(*out, sizeof(collection *) * (size_t)(*n + 1));
    if(grown) {
      *out = grown;
      (*out)[(*n)++] = c;
    }
    else
      coll_free(c);
  }
  jv_free(root);
}

int coll_load_dir(const char *dir, collection ***out, int *n, char *err, size_t errn)
{
  char path[2048];
#ifdef _WIN32
  WIN32_FIND_DATAA d;
  HANDLE h;
  snprintf(path, sizeof(path), "%s\\*.json", dir);
  h = FindFirstFileA(path, &d);
  if(h == INVALID_HANDLE_VALUE)
    return 0;
  do {
    snprintf(path, sizeof(path), "%s\\%s", dir, d.cFileName);
    load_one(path, out, n, err, errn);
  } while(FindNextFileA(h, &d));
  FindClose(h);
#else
  DIR *d = opendir(dir);
  struct dirent *e;
  if(!d)
    return 0;
  while((e = readdir(d))) {
    size_t len = strlen(e->d_name);
    if(len > 5 && !strcmp(e->d_name + len - 5, ".json")) {
      snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
      load_one(path, out, n, err, errn);
    }
  }
  closedir(d);
#endif
  return 0;
}
