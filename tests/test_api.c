/* Unit tests for the API client core. usage: test_api <scratch dir> */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "argfile.h"
#include "builder.h"
#include "fileio.h"
#include "json.h"
#include "mask.h"
#include "options.h"
#include "settings.h"
#include "urlquery.h"
#include "vars.h"

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
  CHECK(args_write_config(&l, path, ARG_CONFIG_LINE_MAX, err, sizeof(err)) == 0);
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
  CHECK(args_write_config(&l, path, ARG_CONFIG_LINE_MAX, err, sizeof(err)) == -1);
  CHECK(strstr(err, "--json") != NULL);
  /* curl before 8.2 reads far less from one line */
  CHECK(args_write_config(&l, path, ARG_CONFIG_LINE_OLD, err, sizeof(err)) == -1);
  CHECK(strstr(err, "0.1 MB") != NULL);
  arg_list_free(&l);
  free(huge);
  request_free(&r);
  remove(path);
}

static void test_args_spill_bodies(void)
{
  request r;
  arg_list l;
  arg_files files;
  char stem[1024], err[256];
  char *body = filled(ARG_BODY_SPILL + 1, 'b'), *at = filled(ARG_BODY_SPILL + 1, 'c');
  size_t len = 0;
  char *got;
  at[0] = '@';
  snprintf(stem, sizeof(stem), "%s/run-9-9", g_dir);
  request_init(&r);
  request_set_value(request_add(&r, options_find("data-raw")), 0, body);
  request_set_value(request_add(&r, options_find("json")), 0, body);
  request_set_value(request_add(&r, options_find("data")), 0, at);   /* a file name already, however odd */
  request_set_value(request_add(&r, options_find("header")), 0, "X: 1");
  CHECK(builder_build(&r, "curl", &l) == 0);
  memset(&files, 0, sizeof(files));
  CHECK(args_spill_bodies(&l, stem, &files, err, sizeof(err)) == 0);
  CHECK(files.n == 2);
  CHECK_STR(l.tok[1].text, "--data-binary");  /* takes the bytes as they are, as --data-raw would */
  CHECK(l.tok[2].text[0] == '@');
  CHECK_STR(l.tok[2].text + 1, files.paths[0]);
  CHECK_STR(l.tok[3].text, "--json");
  CHECK(l.tok[4].text[0] == '@');
  CHECK_STR(l.tok[6].text, at);
  got = file_read_all(files.paths[0], &len, ARG_BODY_SPILL * 2);
  CHECK(got && len == ARG_BODY_SPILL + 1 && !strcmp(got, body));
  free(got);
  args_files_remove(&files);
  CHECK(files.n == 0);
  CHECK(file_read_all(stem, &len, 16) == NULL);
  arg_list_free(&l);
  request_free(&r);
  free(body);
  free(at);
}

static arg_list one_arg_request(const char *url, const char *header)
{
  request r;
  arg_list l;
  request_init(&r);
  if(url)
    request_set_url(&r, url);
  if(header)
    request_set_value(request_add(&r, options_find("header")), 0, header);
  builder_build(&r, "curl", &l);
  request_free(&r);
  return l;
}

static void test_pct_encode(void)
{
  char *s = pct_encode("a b&c=d/e~f.g-h_i");
  CHECK_STR(s, "a%20b%26c%3Dd%2Fe~f.g-h_i");
  free(s);
  s = pct_encode("\xCE\xBA");
  CHECK_STR(s, "%CE%BA");
  free(s);
}

static void test_vars_names(void)
{
  CHECK(vars_name_ok("a_b-c.1"));
  CHECK(!vars_name_ok("a b"));
  CHECK(!vars_name_ok(""));
  CHECK(!vars_name_ok("\"a\":1"));
  CHECK(vars_suggest_secret("API_Token"));
  CHECK(vars_suggest_secret("db_PASSWORD"));
  CHECK(vars_suggest_secret("authHeader"));
  CHECK(!vars_suggest_secret("base"));
}

static void test_vars_missing(void)
{
  var_store st;
  arg_list l = one_arg_request("http://x/{{a}}", "X: {{b}} {{a}} {{\"not\":1}} {{bad name}}");
  str_list m;
  int second;
  vars_init(&st);
  CHECK(st.n == 1);
  CHECK_STR(st.e[0].name, "Default");
  var_set(&st.e[0], "b", "1", 0);
  CHECK(vars_missing(&st.e[st.active], &l, &m) == 0);
  CHECK(m.n == 1);
  CHECK_STR(m.s[0], "a");
  str_list_free(&m);
  /* changing the environment changes what is missing */
  second = env_add(&st, "Other");
  CHECK(second == 1);
  var_set(&st.e[second], "a", "1", 0);
  st.active = second;
  vars_missing(&st.e[st.active], &l, &m);
  CHECK(m.n == 1);
  CHECK_STR(m.s[0], "b");
  str_list_free(&m);
  arg_list_free(&l);
  vars_free(&st);
}

static void test_vars_apply(void)
{
  var_store st;
  var_env *e;
  arg_list l, real, shown;
  char *big;
  vars_init(&st);
  e = &st.e[0];
  var_set(e, "base", "https://h/api", 0);
  var_set(e, "v", "a b&c", 0);
  var_set(e, "t", "s3cret", 1);
  var_set(e, "nested", "{{v}}", 0);
  l = one_arg_request("{{base}}/p?q={{v}}&s={{t}}#{{v}}", "A: {{t}} {{nested}} {{\"x\":1}}");
  CHECK(vars_apply(e, &l, &real, &shown) == 0);
  CHECK(real.count == l.count && shown.count == l.count);
  /* query values are encoded; before ? and after # they go in as they are */
  CHECK_STR(real.tok[real.count - 1].text, "https://h/api/p?q=a%20b%26c&s=s3cret#a b&c");
  CHECK_STR(shown.tok[shown.count - 1].text, "https://h/api/p?q=a%20b%26c&s=********#a b&c");
  CHECK_STR(real.tok[2].text, "A: s3cret {{v}} {{\"x\":1}}");
  CHECK_STR(shown.tok[2].text, "A: ******** {{v}} {{\"x\":1}}");
  CHECK(real.tok[2].kind == TOK_VALUE && shown.tok[2].opt == l.tok[2].opt);
  arg_list_free(&real);
  arg_list_free(&shown);
  arg_list_free(&l);
  /* a value past any fixed buffer arrives whole */
  big = filled(100 * 1024, 'q');
  var_set(e, "big", big, 0);
  l = one_arg_request("http://x/", "X: {{big}}");
  CHECK(vars_apply(e, &l, &real, &shown) == 0);
  CHECK(strlen(real.tok[2].text) == 100 * 1024 + 3);
  arg_list_free(&real);
  arg_list_free(&shown);
  arg_list_free(&l);
  /* a missing name stays visible, it is never sent blank */
  l = one_arg_request("http://x/{{nope}}", NULL);
  CHECK(vars_apply(e, &l, &real, &shown) == 0);
  CHECK_STR(real.tok[real.count - 1].text, "http://x/{{nope}}");
  arg_list_free(&real);
  arg_list_free(&shown);
  arg_list_free(&l);
  free(big);
  vars_free(&st);
}

static void test_vars_store(void)
{
  char path[1024], err[2048], moved[1100];
  var_store a, b;
  size_t len;
  char *got;
  int i;
  snprintf(path, sizeof(path), "%s/variables.json", g_dir);
  remove(path);
  vars_init(&a);
  CHECK(vars_load(&a, path, err, sizeof(err)) == 0);  /* nothing there yet is fine */
  CHECK(a.n == 1);
  var_set(&a.e[0], "one", "1", 0);
  var_set(&a.e[0], "tok", "\xCE\xBA \"q\"", 1);
  var_set(&a.e[0], "tok", "changed", 1);
  CHECK(a.e[0].n == 2);
  i = env_add(&a, "Prod");
  var_set(&a.e[i], "one", "p", 0);
  a.active = i;
  CHECK(env_add(&a, "Prod") == -1);
  CHECK(env_add(&a, "") == -1);
  CHECK(vars_save(&a, path) == 0);
  vars_init(&b);
  CHECK(vars_load(&b, path, err, sizeof(err)) == 0);
  CHECK(b.n == 2 && b.active == 1);
  CHECK_STR(b.e[1].name, "Prod");
  CHECK_STR(var_find(&b.e[0], "tok")->value, "changed");
  CHECK(var_find(&b.e[0], "tok")->secret == 1);
  CHECK(var_find(&b.e[0], "one")->secret == 0);
  vars_free(&b);
  /* copy, rename and removal; the last one stays */
  i = env_copy(&a, 0, "Copy");
  CHECK(i == 2 && a.e[2].n == 2);
  CHECK(env_rename(&a, 2, "Prod") == -1);
  CHECK(env_rename(&a, 2, "Stage") == 0);
  CHECK(env_remove(&a, 2) == 0);
  CHECK(env_remove(&a, 1) == 0);
  CHECK(a.n == 1 && a.active == 0);
  CHECK(env_remove(&a, 0) == -1);
  vars_free(&a);
  /* a damaged file is kept aside, and the store starts over */
  CHECK(file_write_private(path, "{ not json", 10) == 0);
  vars_init(&b);
  err[0] = '\0';
  CHECK(vars_load(&b, path, err, sizeof(err)) == -1);
  CHECK(strstr(err, ".bad-") != NULL);
  CHECK(b.n == 1);
  CHECK_STR(b.e[0].name, "Default");
  CHECK(file_read_all(path, &len, 64) == NULL);
  snprintf(moved, sizeof(moved), "%s.bad-1", path);
  got = file_read_all(moved, &len, 64);
  CHECK(got != NULL);
  free(got);
  remove(moved);
  vars_free(&b);
}

static char *mask_text(const mask_set *m, const char *text)
{
  size_t n = strlen(text);
  char *d = malloc(n + 1);
  memcpy(d, text, n + 1);
  mask_apply(m, d, n);
  CHECK(strlen(d) == n);
  return d;
}

static void test_base64(void)
{
  char *s = base64_encode("user:pass", 9);
  CHECK_STR(s, "dXNlcjpwYXNz");
  free(s);
  s = base64_encode("a", 1);
  CHECK_STR(s, "YQ==");
  free(s);
  s = base64_encode("ab", 2);
  CHECK_STR(s, "YWI=");
  free(s);
}

static void test_mask(void)
{
  var_store st;
  var_env *e;
  arg_list l, real, shown;
  mask_set *m;
  char *got;
  vars_init(&st);
  e = &st.e[0];
  var_set(e, "long", "abcdef", 1);
  var_set(e, "short", "abcd", 1);   /* part of the other: the longer one goes first */
  var_set(e, "tiny", "abc", 1);     /* too short to hide without hiding other text */
  var_set(e, "spaced", "a b+c", 1);
  var_set(e, "plain", "visible", 0);
  l = one_arg_request("http://x/", NULL);
  CHECK(vars_apply(e, &l, &real, &shown) == 0);
  m = mask_build(e, &real);
  got = mask_text(m, "xabcdefy abcd abc a b+c a%20b%2Bc visible");
  CHECK_STR(got, "x******y **** abc ***** ********* visible");
  free(got);
  mask_free(m);
  arg_list_free(&real);
  arg_list_free(&shown);
  arg_list_free(&l);
  /* Basic auth is base64 of user:password on the wire */
  {
    request r;
    var_store st2;
    var_env *e2;
    char *b64 = base64_encode("bob:hunter22", 12);
    char line[128];
    vars_init(&st2);
    e2 = &st2.e[0];
    var_set(e2, "p", "hunter22", 1);
    request_init(&r);
    request_set_url(&r, "http://x/");
    request_set_value(request_add(&r, options_find("user")), 0, "bob:{{p}}");
    builder_build(&r, "curl", &l);
    CHECK(vars_apply(e2, &l, &real, &shown) == 0);
    m = mask_build(e2, &real);
    snprintf(line, sizeof(line), "> Authorization: Basic %s", b64);
    got = mask_text(m, line);
    CHECK(strstr(got, b64) == NULL);
    CHECK(strstr(got, "> Authorization: Basic *") != NULL);
    free(got);
    mask_free(m);
    free(b64);
    arg_list_free(&real);
    arg_list_free(&shown);
    arg_list_free(&l);
    request_free(&r);
    vars_free(&st2);
  }
  /* nothing to hide */
  mask_apply(NULL, NULL, 0);
  vars_free(&st);
}

static void test_query(void)
{
  kv_list rows;
  char *url;
  char *s;
  memset(&rows, 0, sizeof(rows));
  /* a table of plain text becomes an encoded query */
  kv_add(&rows, "q", "\xCE\xBA\xCE\xB1\xCF\x86\xCE\xAD\xCF\x82 & \xCE\xB3\xCE\xAC\xCE\xBB\xCE\xB1", 1);
  kv_add(&rows, "a=b", "c=d", 1);
  kv_add(&rows, "off", "x", 0);
  kv_add(&rows, "t", "{{tok en}}", 1);
  kv_add(&rows, "v", "{{v}}", 1);
  url = query_build("https://h/p?old=1#top", &rows);
  CHECK_STR(url, "https://h/p?q=%CE%BA%CE%B1%CF%86%CE%AD%CF%82%20%26%20%CE%B3%CE%AC%CE%BB%CE%B1"
                 "&a%3Db=c%3Dd&t=%7B%7Btok%20en%7D%7D&v={{v}}#top");
  free(url);
  kv_free(&rows);
  /* and back: decoded, never encoded twice */
  CHECK(query_parse("https://h/p?a=x%20y&b=%7B&c=1+2&d&e=%zz&=z&v={{v}}#top", &rows) == 0);
  CHECK(rows.n == 7);
  CHECK_STR(rows.r[0].key, "a");
  CHECK_STR(rows.r[0].value, "x y");
  CHECK_STR(rows.r[1].value, "{");
  CHECK_STR(rows.r[2].value, "1 2");
  CHECK_STR(rows.r[3].key, "d");
  CHECK_STR(rows.r[3].value, "");
  CHECK_STR(rows.r[4].value, "%zz");
  CHECK_STR(rows.r[5].key, "");
  CHECK_STR(rows.r[6].value, "{{v}}");
  CHECK(rows.r[0].enabled);
  url = query_build("https://h/p?a=x%20y&b=%7B#top", &rows);
  CHECK(url && strstr(url, "?a=x%20y&b=%7B&c=1%202&d=&e=%25zz&=z&v={{v}}#top") != NULL);
  free(url);
  kv_free(&rows);
  /* nothing to show, and every row off drops the ? */
  CHECK(query_parse("https://h/p", &rows) == 0 && rows.n == 0);
  kv_add(&rows, "a", "1", 0);
  url = query_build("https://h/p?x=1", &rows);
  CHECK_STR(url, "https://h/p");
  free(url);
  kv_free(&rows);
  s = pct_decode("a+b%41%zz%", 1);
  CHECK_STR(s, "a bA%zz%");
  free(s);
  s = pct_decode("a+b", 0);
  CHECK_STR(s, "a+b");
  free(s);
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
  test_args_spill_bodies();
  test_args_remove_stale();
  test_pct_encode();
  test_vars_names();
  test_vars_missing();
  test_vars_apply();
  test_vars_store();
  test_base64();
  test_mask();
  test_query();
  printf("%d checks, %d failed\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
