/* API descriptions: loading, each version, and what they become. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fileio.h"
#include "fsdir.h"
#include "oas_load.h"

static int g_fail, g_checks;
static const char *g_fix, *g_scratch;

#define CHECK(c)                                          \
  do {                                                    \
    g_checks++;                                           \
    if(!(c)) {                                            \
      g_fail++;                                           \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
    }                                                     \
  } while(0)
#define CHECK_STR(a, b)                                                     \
  do {                                                                      \
    const char *a_ = (a), *b_ = (b);                                        \
    g_checks++;                                                             \
    if(!a_ || strcmp(a_, b_)) {                                             \
      g_fail++;                                                             \
      printf("FAIL %s:%d:\n  got  [%s]\n  want [%s]\n", __FILE__, __LINE__, \
             a_ ? a_ : "(null)", b_);                                       \
    }                                                                       \
  } while(0)

static char *fix(const char *name)
{
  return fs_join(g_fix, name);
}

/* Loads a fixture, and compares what comes out with the JSON text. */
static void loads(int line, const char *name, const char *json, oas_load_report *rep)
{
  char *path = fix(name), err[512] = "";
  jv *got = oas_load(path, rep, err, sizeof(err));
  jv *want = jv_parse(json, strlen(json), NULL, 0);
  char *a = got ? jv_write(got, 0, NULL) : NULL, *b = jv_write(want, 0, NULL);
  g_checks++;
  if(!a || strcmp(a, b)) {
    g_fail++;
    printf("FAIL line %d: %s: %s\n  got  %s\n  want %s\n", line, name, err, a ? a : "(null)", b);
  }
  free(a);
  free(b);
  free(path);
  jv_free(got);
  jv_free(want);
}

static void ref_same_file(void)
{
  oas_load_report r;
  loads(__LINE__, "refs/same.yaml",
        "{\"a\":{\"v\":1},\"b\":{\"v\":2},\"c\":{\"v\":3},\"d\":{\"v\":4},\"e\":{\"v\":5},"
        "\"defs\":{\"x\":{\"v\":1},\"a/b\":{\"v\":2},\"c~d\":{\"v\":3},\"{id}\":{\"v\":4}},\"list\":[\"zero\",{\"v\":5}]}", &r);
  CHECK(r.files == 1 && !r.broken_refs && !r.remote_refs && !r.cycles);
  oas_load_report_free(&r);
}

static void ref_other_file_relative(void)
{
  oas_load_report r;
  loads(__LINE__, "refs/other.yaml", "{\"p\":{\"n\":3}}", &r);
  CHECK(r.files == 3);
  CHECK(r.outside.n == 0);
  oas_load_report_free(&r);
}

static void ref_siblings_merge(void)
{
  oas_load_report r;
  loads(__LINE__, "refs/siblings.yaml", "{\"a\":{\"description\":\"mine\",\"x\":1},\"d\":{\"description\":\"orig\",\"x\":1}}", &r);
  oas_load_report_free(&r);
}

static void ref_cycle_stops(void)
{
  oas_load_report r;
  char *path = fix("refs/cycle.yaml"), err[256];
  jv *v = oas_load(path, &r, err, sizeof(err));
  CHECK(v != NULL);
  CHECK(r.cycles >= 1);
  CHECK(jv_len(jv_get(jv_get(v, "r"), "next")) == 0);
  CHECK(jv_kind(jv_get(jv_get(v, "r"), "next")) == JV_OBJ);
  jv_free(v);
  free(path);
  oas_load_report_free(&r);
}

static void ref_remote_counted(void)
{
  oas_load_report r;
  loads(__LINE__, "refs/remote.yaml", "{\"a\":{}}", &r);
  CHECK(r.remote_refs == 1);
  oas_load_report_free(&r);
}

static void ref_broken_counted(void)
{
  oas_load_report r;
  loads(__LINE__, "refs/broken.yaml", "{\"a\":{},\"b\":{}}", &r);
  CHECK(r.broken_refs == 2);
  oas_load_report_free(&r);
}

static void ref_outside_folder_listed(void)
{
  oas_load_report r;
  char *out = fix("refs/out.yaml"), *canon = fs_canon(out);
  loads(__LINE__, "refs/in/main.yaml", "{\"a\":{\"v\":1}}", &r);
  CHECK(r.outside.n == 1);
  if(r.outside.n == 1)
    CHECK_STR(r.outside.s[0], canon);
  free(out);
  free(canon);
  oas_load_report_free(&r);
}

static void load_limit_files(void)
{
  char *dir = fs_join(g_scratch, "many"), name[64], body[128], err[256] = "";
  int i;
  oas_load_report r;
  char *first = NULL;
  jv *v;
  for(i = 0; i <= OAS_MAX_FILES; i++) {
    char *p;
    snprintf(name, sizeof(name), "f%d.yaml", i);
    p = fs_join(dir, name);
    snprintf(body, sizeof(body), "a: {$ref: 'f%d.yaml'}\n", i + 1);
    CHECK(!file_write_private(p, body, strlen(body)));
    if(!i)
      first = p;
    else
      free(p);
  }
  v = oas_load(first, &r, err, sizeof(err));
  CHECK(v == NULL);
  CHECK(strstr(err, "256") != NULL);
  jv_free(v);
  free(first);
  free(dir);
  oas_load_report_free(&r);
}

static void load_limit_bytes(void)
{
  char *p = fs_join(g_scratch, "big.yaml"), err[256] = "";
  size_t n = OAS_MAX_BYTES + 16;
  char *body = malloc(n);
  oas_load_report r;
  jv *v;
  memset(body, ' ', n);
  body[n - 1] = '\n';
  CHECK(!file_write_private(p, body, n));
  v = oas_load(p, &r, err, sizeof(err));
  CHECK(v == NULL);
  CHECK(strstr(err, "64 MB") != NULL);
  jv_free(v);
  free(body);
  free(p);
  oas_load_report_free(&r);
}

static void load_refusals(void)
{
  oas_load_report r;
  char *p = fix("refs/none.yaml"), err[256] = "";
  jv *v = oas_load(p, &r, err, sizeof(err));
  CHECK(v == NULL && err[0]);
  jv_free(v);
  free(p);
  oas_load_report_free(&r);
}

static void swagger12_folder(void)
{
  oas_load_report r;
  char *p = fix("refs/s12/api-docs.json"), err[256] = "";
  jv *v = oas_load(p, &r, err, sizeof(err));
  jv *d = jv_get(v, "_declarations");
  CHECK(v != NULL);
  CHECK(jv_len(d) == 2);
  CHECK_STR(jv_str(jv_get(jv_at(d, 0), "resourcePath")), "/pets");
  CHECK_STR(jv_str(jv_get(jv_at(d, 1), "resourcePath")), "/users");
  /* a model's name in $ref is not a file */
  CHECK_STR(jv_str(jv_get(jv_get(jv_get(jv_get(jv_get(jv_at(d, 1), "models"), "U"), "properties"), "p"), "$ref")), "V");
  CHECK(!r.broken_refs);
  jv_free(v);
  free(p);
  oas_load_report_free(&r);
}

int main(int argc, char **argv)
{
  if(argc < 3) {
    fprintf(stderr, "usage: test_openapi FIXTURES SCRATCH\n");
    return 2;
  }
  g_fix = argv[1];
  g_scratch = argv[2];
  ref_same_file();
  ref_other_file_relative();
  ref_siblings_merge();
  ref_cycle_stops();
  ref_remote_counted();
  ref_broken_counted();
  ref_outside_folder_listed();
  load_limit_files();
  load_limit_bytes();
  load_refusals();
  swagger12_folder();
  printf("%d checks, %d failed\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
