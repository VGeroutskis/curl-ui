#include "oas_load.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fileio.h"
#include "fsdir.h"
#include "yaml.h"

#define NODE_BUDGET 2000000L
#define MAX_RECURSION 2000

typedef struct {
  char *path;
  jv *root;
} loaded;

typedef struct {
  loaded *files;
  int nfiles;
  size_t bytes;
  long budget;
  char *home;           /* the folder of the first file */
  str_list stack;       /* the references being followed */
  oas_load_report *rep;
  char *err;
  size_t errn;
  int failed;
} ctx;

static void list_add(str_list *l, const char *s)
{
  char **g = realloc(l->s, sizeof(char *) * (size_t)(l->n + 1));
  size_t n = strlen(s) + 1;
  char *copy = malloc(n);
  if(!g || !copy) {
    if(g)
      l->s = g;
    free(copy);
    return;
  }
  memcpy(copy, s, n);
  l->s = g;
  l->s[l->n++] = copy;
}

static void list_pop(str_list *l)
{
  free(l->s[--l->n]);
}

static void stop(ctx *c, const char *msg, const char *about)
{
  if(!c->failed && c->err && c->errn)
    snprintf(c->err, c->errn, "%s%s%s", about ? about : "", about ? ": " : "", msg);
  c->failed = 1;
}

static int inside_home(const ctx *c, const char *path)
{
  size_t n = strlen(c->home);
  if(strncmp(path, c->home, n))
    return 0;
  return n && (c->home[n - 1] == '/' || c->home[n - 1] == '\\' || path[n] == '/' || path[n] == '\\');
}

/* The file at a canonical path. NULL when it cannot be had: fatal when required, or past a limit. */
static const loaded *load_file(ctx *c, const char *path, int required)
{
  int i;
  size_t len = 0;
  char *text;
  char perr[256] = "";
  jv *root;
  loaded *g;
  for(i = 0; i < c->nfiles; i++)
    if(!strcmp(c->files[i].path, path))
      return &c->files[i];
  if(c->nfiles >= OAS_MAX_FILES) {
    stop(c, "more than 256 files are referred to", NULL);
    return NULL;
  }
  if(fs_kind(path) != FS_FILE) {
    if(required)
      stop(c, "cannot read this file", path);
    return NULL;
  }
  text = file_read_all(path, &len, OAS_MAX_BYTES - c->bytes);
  if(!text) {
    /* the one reason besides the kind checked above: its size */
    FILE *f = fopen(path, "rb");
    if(f) {
      fclose(f);
      stop(c, "the files together are over 64 MB", path);
    }
    else if(required)
      stop(c, "cannot read this file", path);
    return NULL;
  }
  c->bytes += len;
  root = yaml_parse(text, len, perr, sizeof(perr));
  free(text);
  if(!root) {
    stop(c, perr, path);
    return NULL;
  }
  g = realloc(c->files, sizeof(loaded) * (size_t)(c->nfiles + 1));
  if(!g) {
    jv_free(root);
    stop(c, "out of memory", NULL);
    return NULL;
  }
  c->files = g;
  c->files[c->nfiles].path = malloc(strlen(path) + 1);
  if(!c->files[c->nfiles].path) {
    jv_free(root);
    stop(c, "out of memory", NULL);
    return NULL;
  }
  strcpy(c->files[c->nfiles].path, path);
  c->files[c->nfiles].root = root;
  c->nfiles++;
  c->rep->files = c->nfiles;
  if(!inside_home(c, path))
    list_add(&c->rep->outside, path);
  return &c->files[c->nfiles - 1];
}

static int hexval(char h)
{
  return isdigit((unsigned char)h) ? h - '0' : tolower((unsigned char)h) - 'a' + 10;
}

/* #/a/b in a tree. A token has %XX for a URL's sake, then ~1 and ~0. */
static const jv *walk_pointer(const jv *root, const char *ptr)
{
  const jv *at = root;
  const char *c = ptr;
  if(!*c)
    return at;
  if(*c != '/')
    return NULL;
  while(*c == '/' && at) {
    const char *end = ++c;
    char *tok;
    size_t w = 0;
    while(*end && *end != '/')
      end++;
    tok = malloc((size_t)(end - c) + 1);
    if(!tok)
      return NULL;
    for(; c < end; c++) {
      if(*c == '%' && c + 2 < end + 0 && isxdigit((unsigned char)c[1]) && isxdigit((unsigned char)c[2])) {
        tok[w++] = (char)(hexval(c[1]) * 16 + hexval(c[2]));
        c += 2;
      }
      else
        tok[w++] = *c;
    }
    tok[w] = '\0';
    {
      size_t r, o = 0;
      for(r = 0; r < w; r++) {
        if(tok[r] == '~' && tok[r + 1] == '1') {
          tok[o++] = '/';
          r++;
        }
        else if(tok[r] == '~' && tok[r + 1] == '0') {
          tok[o++] = '~';
          r++;
        }
        else
          tok[o++] = tok[r];
      }
      tok[o] = '\0';
    }
    if(jv_kind(at) == JV_OBJ)
      at = jv_get(at, tok);
    else if(jv_kind(at) == JV_ARR) {
      char *e;
      long i = strtol(tok, &e, 10);
      at = *tok && !*e && i >= 0 && i < jv_len(at) ? jv_at(at, (int)i) : NULL;
    }
    else
      at = NULL;
    free(tok);
    c = end;
  }
  return at;
}

static jv *resolve(ctx *c, const jv *node, const char *file, int depth);

static jv *follow(ctx *c, const jv *node, const char *ref, const char *file, int depth)
{
  const char *hash = strchr(ref, '#');
  size_t flen = hash ? (size_t)(hash - ref) : strlen(ref);
  char *target = NULL, *key;
  const jv *at;
  const loaded *lf;
  jv *out = NULL;
  size_t klen;
  int i;
  char *copy_file;
  if(!strncmp(ref, "http://", 7) || !strncmp(ref, "https://", 8) || !strncmp(ref, "//", 2) || !strncmp(ref, "ftp://", 6)) {
    c->rep->remote_refs++;
    return jv_new(JV_OBJ);
  }
  if(flen) {
    char *name = malloc(flen + 1), *up = fs_parent(file), *joined;
    if(!name) {
      free(up);
      stop(c, "out of memory", NULL);
      return NULL;
    }
    memcpy(name, ref, flen);
    name[flen] = '\0';
    joined = fs_resolve(up ? up : ".", name);
    target = joined ? fs_canon(joined) : NULL;
    free(joined);
    free(name);
    free(up);
    if(!target) {
      stop(c, "out of memory", NULL);
      return NULL;
    }
  }
  else {
    target = malloc(strlen(file) + 1);
    if(!target) {
      stop(c, "out of memory", NULL);
      return NULL;
    }
    strcpy(target, file);
  }
  klen = strlen(target) + strlen(hash ? hash : "") + 1;
  key = malloc(klen);
  if(!key) {
    free(target);
    stop(c, "out of memory", NULL);
    return NULL;
  }
  snprintf(key, klen, "%s%s", target, hash ? hash : "");
  for(i = 0; i < c->stack.n; i++) {
    if(!strcmp(c->stack.s[i], key)) {
      c->rep->cycles++;
      free(key);
      free(target);
      return jv_new(JV_OBJ);
    }
  }
  lf = load_file(c, target, 0);
  if(c->failed || !lf) {
    if(!c->failed)
      c->rep->broken_refs++;
    free(key);
    free(target);
    return c->failed ? NULL : jv_new(JV_OBJ);
  }
  at = walk_pointer(lf->root, hash ? hash + 1 : "");
  if(!at) {
    c->rep->broken_refs++;
    free(key);
    free(target);
    return jv_new(JV_OBJ);
  }
  list_add(&c->stack, key);
  free(key);
  /* the list of files may move as it grows: the tree of a file does not, and the name is copied */
  copy_file = target;
  out = resolve(c, at, copy_file, depth + 1);
  list_pop(&c->stack);
  free(target);
  /* what stands beside a $ref goes over what it points to */
  if(out && jv_kind(out) == JV_OBJ) {
    for(i = 0; i < jv_len(node); i++) {
      const char *k = jv_key_at(node, i);
      if(strcmp(k, "$ref")) {
        jv *side = resolve(c, jv_get(node, k), file, depth + 1);
        if(!side) {
          jv_free(out);
          return NULL;
        }
        jv_set(out, k, side);
      }
    }
  }
  return out;
}

static jv *resolve(ctx *c, const jv *node, const char *file, int depth)
{
  jv *out;
  int i;
  if(c->failed)
    return NULL;
  if(--c->budget < 0 || depth > MAX_RECURSION) {
    stop(c, "its references expand to too much", NULL);
    return NULL;
  }
  switch(jv_kind(node)) {
  case JV_OBJ: {
    const char *ref = jv_str(jv_get(node, "$ref"));
    if(ref)
      return follow(c, node, ref, file, depth);
    out = jv_new(JV_OBJ);
    for(i = 0; i < jv_len(node); i++) {
      const char *k = jv_key_at(node, i);
      jv *v = resolve(c, jv_get(node, k), file, depth + 1);
      if(!v) {
        jv_free(out);
        return NULL;
      }
      jv_set(out, k, v);
    }
    return out;
  }
  case JV_ARR:
    out = jv_new(JV_ARR);
    for(i = 0; i < jv_len(node); i++) {
      jv *v = resolve(c, jv_at(node, i), file, depth + 1);
      if(!v) {
        jv_free(out);
        return NULL;
      }
      jv_push(out, v);
    }
    return out;
  default:
    return jv_clone(node);
  }
}

/* A Swagger 1.x listing names its declarations as paths next to it. */
static jv *load_declarations(ctx *c, const jv *root, const char *file)
{
  const jv *apis = jv_get(root, "apis");
  char *dir = fs_parent(file);
  jv *out = jv_new(JV_ARR);
  int i;
  for(i = 0; i < jv_len(apis) && !c->failed; i++) {
    const char *path = jv_str(jv_get(jv_at(apis, i), "path"));
    char name[1024], *full, *canon;
    const loaded *lf = NULL;
    char *fmt;
    if(!path)
      continue;
    while(*path == '/')
      path++;
    snprintf(name, sizeof(name), "%s", path);
    fmt = strstr(name, ".{format}");
    if(fmt)
      strcpy(fmt, ".json");
    full = fs_join(dir ? dir : ".", name);
    canon = full ? fs_canon(full) : NULL;
    if(canon)
      lf = load_file(c, canon, 0);
    free(canon);
    free(full);
    if(!lf && !c->failed) {
      snprintf(name + strlen(name), sizeof(name) - strlen(name), ".json");
      full = fs_join(dir ? dir : ".", name);
      canon = full ? fs_canon(full) : NULL;
      if(canon)
        lf = load_file(c, canon, 0);
      free(canon);
      free(full);
    }
    if(lf)
      jv_push(out, jv_clone(lf->root));
    else if(!c->failed)
      c->rep->broken_refs++;
  }
  free(dir);
  return out;
}

static int is_listing(const jv *root)
{
  const jv *apis = jv_get(root, "apis");
  int i;
  if(!jv_get(root, "swaggerVersion") || jv_kind(apis) != JV_ARR)
    return 0;
  for(i = 0; i < jv_len(apis); i++)
    if(jv_get(jv_at(apis, i), "operations"))
      return 0;
  return 1;
}

jv *oas_load(const char *path, oas_load_report *rep, char *err, size_t errn)
{
  ctx c;
  char *canon, *copy;
  const loaded *first;
  jv *out = NULL;
  int i;
  memset(rep, 0, sizeof(*rep));
  memset(&c, 0, sizeof(c));
  c.rep = rep;
  c.err = err;
  c.errn = errn;
  c.budget = NODE_BUDGET;
  canon = fs_canon(path);
  if(!canon) {
    snprintf(err, errn, "cannot read this file");
    return NULL;
  }
  c.home = fs_parent(canon);
  if(!c.home) {
    free(canon);
    snprintf(err, errn, "cannot read this file");
    return NULL;
  }
  first = load_file(&c, canon, 1);
  if(first) {
    /* resolving may grow the list of files and move this entry */
    const jv *root = first->root;
    copy = canon;
    if(jv_get(root, "swaggerVersion")) {
      out = jv_clone(root);
      if(is_listing(root))
        jv_set(out, "_declarations", load_declarations(&c, root, copy));
    }
    else
      out = resolve(&c, root, copy, 0);
  }
  if(c.failed && out) {
    jv_free(out);
    out = NULL;
  }
  for(i = 0; i < c.nfiles; i++) {
    free(c.files[i].path);
    jv_free(c.files[i].root);
  }
  free(c.files);
  for(i = 0; i < c.stack.n; i++)
    free(c.stack.s[i]);
  free(c.stack.s);
  free(c.home);
  free(canon);
  return out;
}

void oas_load_report_free(oas_load_report *r)
{
  str_list_free(&r->outside);
}
