/* Unit tests for the API client core. usage: test_api <scratch dir> */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "fileio.h"
#include "json.h"

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

int main(int argc, char **argv)
{
  g_dir = argc > 1 ? argv[1] : ".";
  test_jv_roundtrip();
  test_jv_rejects();
  test_jv_build();
  test_file_private();
  printf("%d checks, %d failed\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
