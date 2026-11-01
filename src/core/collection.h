/* Saved requests, in trees of folders. One file per collection, named by a generated id and
   never by anything the user typed. */
#ifndef CURLUI_COLLECTION_H
#define CURLUI_COLLECTION_H

#include <stddef.h>

#include "json.h"

#define COLL_VERSION 1

typedef enum { NODE_FOLDER, NODE_REQUEST } node_kind;

typedef struct coll_node {
  node_kind kind;
  char *name;
  char id[17];
  jv *req;                 /* NODE_REQUEST: see reqjson.h */
  struct coll_node **kids;
  int nkids, capkids;
  struct coll_node *parent;
  int open;                /* in the tree on screen */
} coll_node;

typedef struct {
  char id[17];
  coll_node *root;         /* a folder holding the collection's name */
  int read_only;           /* from a newer version: shown, never overwritten */
} collection;

collection *coll_new(const char *name);
/* Damaged files are moved aside and named in err. A missing directory is an empty list. */
int coll_load_dir(const char *dir, collection ***out, int *n, char *err, size_t errn);
int coll_save(const collection *c, const char *dir);
void coll_delete_file(const collection *c, const char *dir);
void coll_free(collection *c);

coll_node *coll_add(coll_node *parent, node_kind k, const char *name, jv *req);  /* takes req */
void coll_remove(coll_node *n);
coll_node *coll_copy(const coll_node *n);        /* next to it, under a new id */
int coll_move(coll_node *n, coll_node *new_parent);  /* refuses a request, or n's own subtree */
int coll_rename(coll_node *n, const char *name);
coll_node *coll_find(coll_node *root, const char *id);
collection *coll_of(collection **list, int n, const coll_node *node);

void coll_make_id(char out[17]);

#endif
