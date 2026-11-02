/* The YAML that API descriptions use, each refusal, and the limits. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "yaml.h"

static int g_fail, g_checks;

static void same(int line, const char *yaml, const char *json)
{
  char err[256] = "";
  jv *got = yaml_parse(yaml, strlen(yaml), err, sizeof(err));
  jv *want = jv_parse(json, strlen(json), NULL, 0);
  char *a = got ? jv_write(got, 0, NULL) : NULL, *b = want ? jv_write(want, 0, NULL) : NULL;
  g_checks++;
  if(!a || !b || strcmp(a, b)) {
    g_fail++;
    printf("FAIL line %d: %s\n  got  %s\n  want %s\n", line, err, a ? a : "(null)", b ? b : "(bad test)");
  }
  free(a);
  free(b);
  jv_free(got);
  jv_free(want);
}
#define SAME(y, j) same(__LINE__, y, j)

static void refused(int cline, const char *yaml, const char *where)
{
  char err[256] = "";
  jv *got = yaml_parse(yaml, strlen(yaml), err, sizeof(err));
  g_checks++;
  if(got || !strstr(err, where)) {
    g_fail++;
    printf("FAIL line %d: expected a refusal with [%s], got [%s]\n", cline, where, err);
  }
  jv_free(got);
}
#define REFUSED(y, w) refused(__LINE__, y, w)

static void yaml_maps_and_lists(void)
{
  SAME("a: 1\nb:\n  c: x\n  d:\n    - 1\n    - two\n", "{\"a\":1,\"b\":{\"c\":\"x\",\"d\":[1,\"two\"]}}");
  SAME("- a\n- - b\n  - c\n- d: 1\n  e: 2\n", "[\"a\",[\"b\",\"c\"],{\"d\":1,\"e\":2}]");
  SAME("a:\nb: 1\n", "{\"a\":null,\"b\":1}");
  SAME("plain", "\"plain\"");
}

static void yaml_flow(void)
{
  SAME("a: [1, x, {b: 2}]\nc: {d: [], e: \"f\", g}\n", "{\"a\":[1,\"x\",{\"b\":2}],\"c\":{\"d\":[],\"e\":\"f\",\"g\":null}}");
  SAME("a: [\n  1, # one\n  2,\n]\n", "{\"a\":[1,2]}");
  SAME("{\"a\":1,\"b\":[true,null]}", "{\"a\":1,\"b\":[true,null]}");
  SAME("{a: 1}", "{\"a\":1}");
  SAME("a: {\"b\":1}", "{\"a\":{\"b\":1}}");
}

static void yaml_scalars(void)
{
  SAME("- true\n- False\n- null\n- ~\n- 1\n- -2\n- 1.5\n- 1e3\n- \"1\"\n- 'it''s'\n- 3.0.1\n- 0x10\n",
       "[true,false,null,null,1,-2,1.5,1000,\"1\",\"it's\",\"3.0.1\",16]");
  SAME("- \"\\u00e9\\n\\t\\\"\\\\\"\n- \"\\xe9\"\n- \"\\U0001F600\"\n", "[\"\\u00e9\\n\\t\\\"\\\\\",\"\\u00e9\",\"\\ud83d\\ude00\"]");
  SAME("a: http://x:8/y?z=1#f\nb: 'x: y'\nc: a-b\nd: -x\n", "{\"a\":\"http://x:8/y?z=1#f\",\"b\":\"x: y\",\"c\":\"a-b\",\"d\":\"-x\"}");
  SAME("!!str 1", "\"1\"");
  SAME("a: !!str true\n", "{\"a\":\"true\"}");
  SAME("'a\n  b'", "\"a b\"");
  SAME("\"a\n\n  b \\\n  c\"", "\"a\\nb c\"");
}

static void yaml_block_scalars(void)
{
  SAME("a: |\n  x\n   y\n\n  z\n\nb: 1\n", "{\"a\":\"x\\n y\\n\\nz\\n\",\"b\":1}");
  SAME("a: |-\n  x\n\n", "{\"a\":\"x\"}");
  SAME("a: |+\n  x\n\nb: 1\n", "{\"a\":\"x\\n\\n\",\"b\":1}");
  SAME("a: >\n  x\n  y\n\n  z\n", "{\"a\":\"x y\\nz\\n\"}");
  SAME("a: >-\n  x\n  y\n", "{\"a\":\"x y\"}");
  SAME("a: >\n  x\n    more\n  y\n", "{\"a\":\"x\\n  more\\ny\\n\"}");
  SAME("a: |2\n    x\n", "{\"a\":\"  x\\n\"}");
  SAME("- |\n  x\n- y\n", "[\"x\\n\",\"y\"]");
  SAME("a: | # note\n  # not a comment\n", "{\"a\":\"# not a comment\\n\"}");
  SAME("a: |\nb: 1\n", "{\"a\":\"\",\"b\":1}");
}

static void yaml_multiline_plain(void)
{
  SAME("a: one\n  two\n\n  three\nb: 1\n", "{\"a\":\"one two\\nthree\",\"b\":1}");
}

static void yaml_comments(void)
{
  SAME("# top\na: b # c\n# mid\nd: e#f\n  # deep\ng: 1\n", "{\"a\":\"b\",\"d\":\"e#f\",\"g\":1}");
}

static void yaml_anchor_alias_merge(void)
{
  SAME("a: &x {k: 1}\nb: *x\n", "{\"a\":{\"k\":1},\"b\":{\"k\":1}}");
  SAME("base: &b\n  x: 1\n  y: 2\nuse:\n  <<: *b\n  y: 3\n", "{\"base\":{\"x\":1,\"y\":2},\"use\":{\"y\":3,\"x\":1}}");
  SAME("p: &p {x: 1}\nq: &q {x: 2, y: 2}\nu:\n  <<: [*p, *q]\n", "{\"p\":{\"x\":1},\"q\":{\"x\":2,\"y\":2},\"u\":{\"x\":1,\"y\":2}}");
  SAME("s: &s hello\nt: *s\n", "{\"s\":\"hello\",\"t\":\"hello\"}");
}

static void yaml_real_shapes(void)
{
  SAME("---\nopenapi: 3.0.0\n", "{\"openapi\":\"3.0.0\"}");
  SAME("%YAML 1.2\n---\na: 1\n...\n", "{\"a\":1}");
  SAME("parameters:\n- name: id\n  in: path\n- name: q\nnext: 1\n",
       "{\"parameters\":[{\"name\":\"id\",\"in\":\"path\"},{\"name\":\"q\"}],\"next\":1}");
  SAME("paths:\n  /pets/{id}:\n    get:\n      responses:\n        '200':\n          description: ok\n        404:\n          description: no\n",
       "{\"paths\":{\"/pets/{id}\":{\"get\":{\"responses\":{\"200\":{\"description\":\"ok\"},\"404\":{\"description\":\"no\"}}}}}}");
  SAME("content:\n  application/json:\n    schema: {$ref: '#/components/schemas/Pet'}\n",
       "{\"content\":{\"application/json\":{\"schema\":{\"$ref\":\"#/components/schemas/Pet\"}}}}");
  SAME("x: []\ny: {}\n\"quoted key\": 1\n'single': 2\n", "{\"x\":[],\"y\":{},\"quoted key\":1,\"single\":2}");
  SAME("a: 1\r\nb:\r\n  - 2\r\n", "{\"a\":1,\"b\":[2]}");
  SAME("\xEF\xBB\xBF" "a: 1\n", "{\"a\":1}");
  SAME("description: |\n  A long text:\n    - with: things\n  # and this\nname: x\n",
       "{\"description\":\"A long text:\\n  - with: things\\n# and this\\n\",\"name\":\"x\"}");
  SAME("", "null");
  SAME("# nothing\n", "null");
}

static void yaml_refusals(void)
{
  REFUSED("a:\n\tb: 1\n", "line 2");
  REFUSED("a: 1\n---\nb: 2\n", "line 2");
  REFUSED("a: !custom x\n", "line 1");
  REFUSED("a:\n    b: 1\n  c: 2\n", "line 3");
  REFUSED("a: *nope\n", "line 1");
  REFUSED("a: b: c\n", "line 1");
  REFUSED("a: [1, 2\n", "line");
  REFUSED("a: 'open\n", "line");
  REFUSED("? complex\n: x\n", "line 1");
  REFUSED("a: \"\\q\"\n", "line 1");
  REFUSED("a: \"\\u0000\"\n", "line 1");
  REFUSED("a: @x\n", "line 1");
  REFUSED("- a\nb: 1\n", "line 2");
}

static void yaml_nesting_limit(void)
{
  char *ok = malloc(100000), *deep = malloc(100000);
  size_t n = 0;
  int i, k;
  char err[256];
  jv *v;
  for(i = 0; i < YAML_MAX_DEPTH - 1; i++) {
    for(k = 0; k < i; k++)
      ok[n++] = ' ';
    n += (size_t)sprintf(ok + n, "a:\n");
  }
  for(k = 0; k < i; k++)
    ok[n++] = ' ';
  n += (size_t)sprintf(ok + n, "a: 1\n");
  v = yaml_parse(ok, n, err, sizeof(err));
  g_checks++;
  if(!v) {
    g_fail++;
    printf("FAIL nesting 128 refused: %s\n", err);
  }
  jv_free(v);
  n = 0;
  for(i = 0; i < YAML_MAX_DEPTH + 2; i++) {
    for(k = 0; k < i; k++)
      deep[n++] = ' ';
    n += (size_t)sprintf(deep + n, "a:\n");
  }
  deep[n] = '\0';
  REFUSED(deep, "deep");
  n = 0;
  for(i = 0; i < 5000; i++)
    deep[n++] = '[';
  deep[n] = '\0';
  REFUSED(deep, "deep");
  free(ok);
  free(deep);
}

static void yaml_alias_bomb(void)
{
  char *t = malloc(4096);
  size_t n = (size_t)sprintf(t, "a0: &a0 [x]\n");
  int i;
  clock_t t0 = clock();
  for(i = 1; i < 10; i++)
    n += (size_t)sprintf(t + n, "a%d: &a%d [*a%d,*a%d,*a%d,*a%d,*a%d,*a%d,*a%d,*a%d,*a%d]\n", i, i, i - 1, i - 1, i - 1, i - 1, i - 1, i - 1, i - 1, i - 1, i - 1);
  REFUSED(t, "alias");
  g_checks++;
#ifdef __SANITIZE_ADDRESS__
  /* a sanitizer makes it several times slower */
  if((double)(clock() - t0) / CLOCKS_PER_SEC > 15.0) {
#else
  if((double)(clock() - t0) / CLOCKS_PER_SEC > 1.0) {
#endif
    g_fail++;
    printf("FAIL the alias bomb took over a second\n");
  }
  free(t);
}

int main(void)
{
  yaml_maps_and_lists();
  yaml_flow();
  yaml_scalars();
  yaml_block_scalars();
  yaml_multiline_plain();
  yaml_comments();
  yaml_anchor_alias_merge();
  yaml_real_shapes();
  yaml_refusals();
  yaml_nesting_limit();
  yaml_alias_bomb();
  printf("%d checks, %d failed\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
