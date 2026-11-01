/* Unit tests for the API client core. usage: test_api <scratch dir> */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "argfile.h"
#include "builder.h"
#include "fileio.h"
#include "json.h"
#include "options.h"
#include "settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <sys/stat.h>
#endif

static int g_fail, g_checks;
#define CHECK(c)                                                  \
  do {                                                            \
    g_checks++;                                                   \
    if(!(c)) {                                                    \
      g_fail++;                                                   \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);         \
    }                                                             \
  } while(0)
#define CHECK_STR(a, b)                                                       \
  do {                                                                        \
    const char *a_ = (a), *b_ = (b);                                          \
    g_checks++;                                                               \
    if(!a_ || strcmp(a_, b_)) {                                               \
      g_fail++;                                                               \
      printf("FAIL %s:%d:\n  got  [%s]\n  want [%s]\n", __FILE__, __LINE__,   \
             a_ ? a_ : "(null)", b_);                                         \
    }                                                                         \
  } while(0)

static const char *g_dir;

static jv *parse(const char *s, char *err, size_t n)
{
  return jv_parse(s, strlen(s), err, n);
}

static void test_jv_roundtrip(void)
{
  char err[128];
  const char *src = "{\"a\":[1,true,null,\"\xC3\xA9\\n\\u00e9\\ud83d\\ude00\"],\"z\":{\"k\":-1.5e2},\"b\":\"\"}";
  jv *v = parse(src, err, sizeof(err));
  CHECK(v != NULL);
  if(!v)
    return;
  CHECK(jv_kind(v) == JV_OBJ);
  CHECK(jv_len(jv_get(v, "a")) == 4);
  CHECK(jv_num(jv_at(jv_get(v, "a"), 0)) == 1);
  CHECK(jv_bool(jv_at(jv_get(v, "a"), 1)) == 1);
  CHECK(jv_kind(jv_at(jv_get(v, "a"), 2)) == JV_NULL);
  CHECK_STR(jv_str(jv_at(jv_get(v, "a"), 3)), "\xC3\xA9\n\xC3\xA9\xF0\x9F\x98\x80");
  CHECK(jv_num(jv_get(jv_get(v, "z"), "k")) == -150);
  CHECK(jv_get(v, "x") == NULL);
  CHECK(jv_kind(jv_get(v, "x")) == JV_NULL);
  CHECK_STR(jv_key_at(v, 0), "a");
  CHECK_STR(jv_key_at(v, 2), "b");  /* order kept */
  {
    size_t n;
    char *out = jv_write(v, 0, &n);
    jv *again = out ? jv_parse(out, n, err, sizeof(err)) : NULL;
    CHECK(again != NULL);
    CHECK_STR(jv_str(jv_at(jv_get(again, "a"), 3)), "\xC3\xA9\n\xC3\xA9\xF0\x9F\x98\x80");
    CHECK_STR(jv_key_at(again, 1), "z");
    jv_free(again);
    free(out);
  }
  jv_free(v);
}

static void test_jv_rejects(void)
{
  static const char *const bad[] = {
    "", "{", "[1,]", "{\"a\":1,}", "1 2", "\"open", "tru", "{a:1}", "\"\\ud83d\"",
    "\"\\x\"", "01", "\"tab\there\"", NULL
  };
  char err[128];
  char deep[200];
  int i;
  for(i = 0; bad[i]; i++) {
    jv *v;
    err[0] = '\0';
    v = parse(bad[i], err, sizeof(err));
    if(v)
      printf("accepted: %s\n", bad[i]);
    CHECK(v == NULL);
    CHECK(strstr(err, "line") != NULL);
    jv_free(v);
  }
  /* depth: 64 fits, 65 does not */
  memset(deep, '[', 64);
  memset(deep + 64, ']', 64);
  deep[128] = '\0';
  {
    jv *v = parse(deep, err, sizeof(err));
    CHECK(v != NULL);
    jv_free(v);
  }
  memset(deep, '[', 65);
  memset(deep + 65, ']', 65);
  deep[130] = '\0';
  CHECK(parse(deep, err, sizeof(err)) == NULL);
  /* line and column point at the problem */
  parse("{\n  \"a\": x\n}", err, sizeof(err));
  CHECK(strstr(err, "line 2") != NULL);
}

static void test_jv_build(void)
{
  jv *o = jv_new(JV_OBJ), *a = jv_new(JV_ARR);
  size_t n;
  char *s;
  jv_push(a, jv_new_str("q\"\\\n\x01"));
  jv_push(a, jv_new_num(3));
  jv_push(a, jv_new_bool(0));
  jv_set(o, "list", a);
  jv_set(o, "k", jv_new_str("1"));
  jv_set(o, "k", jv_new_str("2"));  /* replaces */
  s = jv_write(o, 0, &n);
  CHECK_STR(s, "{\"list\":[\"q\\\"\\\\\\n\\u0001\",3,false],\"k\":\"2\"}");
  CHECK(n == strlen(s));
  free(s);
  s = jv_write(o, 1, &n);
  CHECK(s && strstr(s, "\n  \"k\": \"2\"") != NULL);
  free(s);
  jv_free(o);
}

static void test_file_private(void)
{
  char path[1024], moved[1100];
  size_t len = 0;
  char *got;
  snprintf(path, sizeof(path), "%s/sub/dir/f.txt", g_dir);
  CHECK(file_write_private(path, "hello", 5) == 0);
  got = file_read_all(path, &len, 1024);
  CHECK(got && len == 5);
  CHECK_STR(got, "hello");
  free(got);
  CHECK(file_read_all(path, &len, 2) == NULL);  /* too big */
#ifndef _WIN32
  {
    struct stat st;
    CHECK(stat(path, &st) == 0 && (st.st_mode & 0777) == 0600);
  }
#endif
  /* replaced whole, and quarantine never overwrites */
  CHECK(file_write_private(path, "two", 3) == 0);
  CHECK(file_quarantine(path, moved, sizeof(moved)) == 0);
  CHECK(strstr(moved, ".bad-") != NULL);
  CHECK(file_read_all(path, &len, 1024) == NULL);
  got = file_read_all(moved, &len, 1024);
  CHECK_STR(got, "two");
  free(got);
  {
    char moved2[1100];
    CHECK(file_write_private(path, "three", 5) == 0);
    CHECK(file_quarantine(path, moved2, sizeof(moved2)) == 0);
    CHECK(strcmp(moved, moved2) != 0);
    got = file_read_all(moved, &len, 1024);
    CHECK_STR(got, "two");
    free(got);
    remove(moved2);
  }
  remove(moved);
}

static char *filled(size_t n, char c)
{
  char *s = malloc(n + 1);
  memset(s, c, n);
  s[n] = '\0';
  return s;
}

static void test_values_have_no_size_limit(void)
{
  request r;
  arg_list l;
  sel_option *s;
  size_t big = 5u * 1024 * 1024;
  char *text = filled(big, 'x'), *url = filled(100000, 'u');
  int i, found = 0;
  request_init(&r);
  CHECK(r.url != NULL && r.url[0] == '\0');
  s = request_add(&r, options_find("data"));
  CHECK(request_set_value(s, 0, text) != NULL);
  CHECK(strlen(s->values[0]) == big);
  CHECK(request_append_value(s, "second") != NULL);
  CHECK_STR(s->values[1], "second");
  CHECK(request_set_url(&r, url) == 0);
  CHECK(strlen(r.url) == 100000);
  CHECK(builder_build(&r, "curl", &l) == 0);
  for(i = 0; i < l.count; i++)
    if(l.tok[i].kind == TOK_VALUE && strlen(l.tok[i].text) == big)
      found = 1;
  CHECK(found);
  CHECK(strlen(l.tok[l.count - 1].text) == 100000);
  arg_list_free(&l);
  /* shrinking leaves it usable, and a value can be cleared */
  CHECK(request_set_value(s, 0, "") != NULL);
  CHECK(s->values[0][0] == '\0');
  request_free(&r);
  free(text);
  free(url);
}

static void test_request_file_has_no_line_limit(void)
{
  char path[1024];
  request a, b;
  int method = 0;
  size_t big = 5u * 1024 * 1024;
  char *text = filled(big, 'y');
  snprintf(path, sizeof(path), "%s/big-request.txt", g_dir);
  request_init(&a);
  request_init(&b);
  request_set_url(&a, "http://x/");
  request_set_value(request_add(&a, options_find("data")), 0, text);
  CHECK(request_save(&a, 0, path) == 0);
  CHECK(request_load(&b, &method, path) == 1);
  CHECK(b.nsel == 1 && strlen(b.sel[0].values[0]) == big);
  CHECK_STR(b.url, "http://x/");
  request_free(&a);
  request_free(&b);
  remove(path);
  free(text);
}

static void test_args_need_file(void)
{
  char *small = filled(99000, 'a'), *large = filled(101000, 'a'), *part = filled(50000, 'a');
  const char *one_small[] = { "curl", "--data", small, NULL };
  const char *one_large[] = { "curl", "--data", large, NULL };
  const char *many[] = { "curl", part, part, part, part, part, NULL };
  const char *tiny[] = { "curl", "--data", "x", NULL };
  CHECK(!args_need_file(tiny));
  CHECK(args_need_file(one_large));
  CHECK(args_need_file(many));
#ifdef _WIN32
  CHECK(args_need_file(one_small));  /* the command line limit comes first */
#else
  CHECK(!args_need_file(one_small));
#endif
  free(small);
  free(large);
  free(part);
}

static void test_args_write_config(void)
{
  request r;
  arg_list l;
  char path[1024], err[256];
  size_t len = 0;
  char *got, *huge;
  snprintf(path, sizeof(path), "%s/run.conf", g_dir);
  request_init(&r);
  request_add(&r, options_find("disable"));
  request_add(&r, options_find("location"));
  request_set_value(request_add(&r, options_find("data-raw")), 0, "a\"b\\c\nd\te\rf\vg");
  request_set_url(&r, "-http://x/");
  CHECK(builder_build(&r, "curl", &l) == 0);
  CHECK(args_write_config(&l, path, err, sizeof(err)) == 0);
  got = file_read_all(path, &len, 4096);
  /* --disable stays on the command line, where curl honours it */
  CHECK_STR(got, "--location\n--data-raw \"a\\\"b\\\\c\\nd\\te\\rf\\vg\"\n--url \"-http://x/\"\n");
  free(got);
  arg_list_free(&l);
  /* over what curl reads from one config line: refused, naming the option */
  huge = filled(ARG_CONFIG_LINE_MAX + 1, 'z');
  request_set_value(request_add(&r, options_find("json")), 0, huge);
  CHECK(builder_build(&r, "curl", &l) == 0);
  err[0] = '\0';
  CHECK(args_write_config(&l, path, err, sizeof(err)) == -1);
  CHECK(strstr(err, "--json") != NULL);
  arg_list_free(&l);
  free(huge);
  request_free(&r);
  remove(path);
}

static void test_args_remove_stale(void)
{
  char a[1024], b[1024];
  size_t len;
  char *got;
  snprintf(a, sizeof(a), "%s/run-1-2.conf", g_dir);
  snprintf(b, sizeof(b), "%s/keep.conf", g_dir);
  file_write_private(a, "x", 1);
  file_write_private(b, "y", 1);
  args_remove_stale(g_dir);
  CHECK(file_read_all(a, &len, 16) == NULL);
  got = file_read_all(b, &len, 16);
  CHECK(got != NULL);
  free(got);
  remove(b);
}

int main(int argc, char **argv)
{
  g_dir = argc > 1 ? argv[1] : ".";
  test_jv_roundtrip();
  test_jv_rejects();
  test_jv_build();
  test_file_private();
  test_values_have_no_size_limit();
  test_request_file_has_no_line_limit();
  test_args_need_file();
  test_args_write_config();
  test_args_remove_stale();
  printf("%d checks, %d failed\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
