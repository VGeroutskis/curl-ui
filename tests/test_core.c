/* Unit tests for the SDL-free core. usage: test_core <echoargs> [curl] */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "builder.h"
#include "curlinfo.h"
#include "docs.h"
#include "json_fmt.h"
#include "options.h"
#include "osthread.h"
#include "outbuf.h"
#include "procrun.h"
#include "settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

static const char *g_echoargs, *g_curl;

static void nap(int ms)
{
#ifdef _WIN32
  Sleep((DWORD)ms);
#else
  struct timespec ts;
  ts.tv_sec = ms / 1000;
  ts.tv_nsec = (long)(ms % 1000) * 1000000L;
  nanosleep(&ts, NULL);
#endif
}

static void test_options(void)
{
  int ua = options_find("user-agent");
  static int results[1024];
  int i, n;
  CHECK(g_option_count == 278);
  for(i = 1; i < g_option_count; i++)
    CHECK(strcmp(g_options[i - 1].name, g_options[i].name) < 0);
  CHECK(ua >= 0);
  CHECK(options_find("--user-agent") == ua);
  CHECK(options_find("nonsense") < 0);
  CHECK(options_find_n("user-agent-and-more", 10) == ua);
  CHECK_STR(g_options[ua].short_flag, "A");
  CHECK_STR(g_options[ua].arg, "<name>");
  CHECK_STR(g_options[ua].protocols, "HTTP");
  CHECK_STR(g_options[ua].added, "4.5.1");
  CHECK(g_options[ua].kind == OPT_KIND_SINGLE);
  CHECK(!g_options[ua].repeatable);
  CHECK_STR(g_options[ua].neg, "");
  CHECK(g_options[options_find("header")].repeatable);
  CHECK_STR(g_options[options_find("location")].neg, "no-location");
  CHECK_STR(g_options[options_find("no-alpn")].neg, "alpn");
  /* the docs call this one boolean; curl refuses --no-http2-prior-knowledge */
  CHECK_STR(g_options[options_find("http2-prior-knowledge")].neg, "");
  CHECK_STR(g_options[options_find("remote-name")].neg, "no-remote-name");
  CHECK_STR(g_options[options_find("version")].neg, "");
  CHECK(g_options[options_find("http2")].mutexes != NULL);
  CHECK_STR(g_options[options_find("http2")].mutexes[0], "http1.1");
  CHECK(strstr(g_options[options_find("cookie")].placeholder, "e.g. ") != NULL);
  for(i = 0; i < g_option_count; i++) {
    const opt_def *o = &g_options[i];
    CHECK(o->help[0] && o->description[0] && o->categories[0] && o->added[0]);
    CHECK(strncmp(o->description, "# ", 2));  /* the heading is stripped */
    if(o->examples)
      CHECK(!strstr(o->examples[0], "$URL"));
  }
  /* every cross reference resolves, or the docs panel links would dangle */
  for(i = 0; i < g_option_count; i++) {
    const char *const *m = g_options[i].mutexes;
    while(m && *m)
      CHECK(options_find(*m++) >= 0);
  }

  n = options_search("", OPT_FILTER_ALL, results);
  CHECK(n > 200 && n < 278);  /* deprecated hidden */
  for(i = 0; i < n; i++)
    CHECK(!g_options[results[i]].deprecated);
  n = options_search("agent", OPT_FILTER_ALL, results);
  CHECK(n >= 2);
  CHECK(strstr(g_options[results[0]].name, "agent") != NULL);
  n = options_search("--user-agent", OPT_FILTER_ALL, results);
  CHECK(n >= 1 && results[0] == ua);
  n = options_search("", "dns", results);
  CHECK(n == 11);
  n = options_search("", OPT_FILTER_POPULAR, results);
  CHECK(n >= 14);
  /* searching does reach deprecated options */
  n = options_search("ntlm-wb", OPT_FILTER_ALL, results);
  CHECK(n >= 1 && g_options[results[0]].deprecated);
  CHECK(options_search("zzzznothing", OPT_FILTER_ALL, results) == 0);
  CHECK(options_in_category(&g_options[ua], "http"));
  CHECK(!options_in_category(&g_options[ua], "htt"));
}

static void test_builder(void)
{
  request r;
  arg_list l;
  char *prev;
  sel_option *s;
  const char *extra[] = { "-v", NULL };
  const char **argv;

  request_init(&r);
  request_set_url(&r, "https://example.com/?a=1&b=2");
  CHECK(builder_build(&r, "curl", &l) == 0);
  prev = builder_preview(&l);
  CHECK_STR(prev, "curl 'https://example.com/?a=1&b=2'");
  free(prev);
  arg_list_free(&l);

  snprintf(r.method, sizeof(r.method), "POST");
  s = request_add(&r, options_find("header"));
  request_set_value(s, 0, "X-A: it's");
  request_append_value(s, "X-B: 2");
  request_add_value(s);  /* empty: unfinished, left out */
  request_add(&r, options_find("location"));
  request_add(&r, options_find("compressed"));
  CHECK(request_add(&r, options_find("location")) != NULL);
  CHECK(r.nsel == 3);
  r.sel[request_find(&r, options_find("location"))].negated = 1;
  CHECK(builder_build(&r, "curl", &l) == 0);
  prev = builder_preview(&l);
  CHECK_STR(prev, "curl -X POST --header 'X-A: it'\\''s' --header 'X-B: 2' "
                  "--no-location --compressed 'https://example.com/?a=1&b=2'");
  free(prev);
  CHECK(l.tok[0].kind == TOK_PROGRAM);
  CHECK(l.tok[1].kind == TOK_FLAG);
  CHECK(l.tok[2].kind == TOK_VALUE);
  CHECK(l.tok[l.count - 1].kind == TOK_URL);
  CHECK(l.tok[3].opt == options_find("header"));
  /* argv is literal: quoting exists only in the preview */
  CHECK_STR(l.tok[4].text, "X-A: it's");
  argv = arg_list_argv(&l, extra);
  CHECK_STR(argv[l.count - 1], "-v");
  CHECK_STR(argv[l.count], r.url);
  CHECK(argv[l.count + 1] == NULL);
  free((void *)argv);
  arg_list_free(&l);

  /* an explicit --request wins over the method combo */
  s = request_add(&r, options_find("request"));
  request_set_value(s, 0, "PUT");
  CHECK(builder_build(&r, "curl", &l) == 0);
  prev = builder_preview(&l);
  CHECK(strstr(prev, "-X POST") == NULL);
  CHECK(strstr(prev, "--request PUT") != NULL);
  free(prev);
  arg_list_free(&l);

  request_clear_options(&r);
  CHECK(r.nsel == 0);
  snprintf(r.method, sizeof(r.method), "HEAD");
  request_set_url(&r, "-weird");
  CHECK(builder_build(&r, "curl", &l) == 0);
  prev = builder_preview(&l);
  CHECK_STR(prev, "curl --head --url -weird");
  free(prev);
  argv = arg_list_argv(&l, extra);
  CHECK_STR(argv[2], "-v");  /* extras stay ahead of the --url pair */
  CHECK_STR(argv[3], "--url");
  free((void *)argv);
  arg_list_free(&l);

  s = request_add(&r, options_find("output"));
  CHECK(s->nvalues == 1);
  request_remove_value(s, 0);
  CHECK(s->nvalues == 0);
  request_remove(&r, options_find("output"));
  CHECK(r.nsel == 0);
  request_free(&r);

  prev = shell_quote("");
  CHECK_STR(prev, "''");
  free(prev);
  prev = shell_quote("a$b`c\"d");
  CHECK_STR(prev, "'a$b`c\"d'");
  free(prev);
}

static void test_url_checks(void)
{
  request r;
  sel_option *s;
  request_init(&r);
  CHECK(request_lacks_url(&r));
  s = request_add(&r, options_find("abstract-unix-socket"));
  request_set_value(s, 0, "https://examples.com");
  CHECK(request_lacks_url(&r));
  s = request_add(&r, options_find("url"));
  CHECK(request_lacks_url(&r));  /* --url with no value yet */
  request_set_value(s, 0, "http://x/");
  CHECK(!request_lacks_url(&r));
  request_clear_options(&r);
  request_add(&r, options_find("version"));
  CHECK(!request_lacks_url(&r));
  request_clear_options(&r);
  /* these run without a URL too */
  request_add(&r, options_find("dump-ca-embed"));
  CHECK(!request_lacks_url(&r));
  request_clear_options(&r);
  request_set_value(request_add(&r, options_find("engine")), 0, "list");
  CHECK(!request_lacks_url(&r));
  request_clear_options(&r);
  request_add(&r, options_find("help"));  /* its value is optional */
  CHECK(!request_lacks_url(&r));
  request_clear_options(&r);
  r.config_file = "my.conf";
  CHECK(!request_lacks_url(&r));
  r.config_file = NULL;
  request_set_url(&r, "http://x/");
  CHECK(!request_lacks_url(&r));
  request_free(&r);

  CHECK(value_looks_like_url("https://examples.com"));
  CHECK(value_looks_like_url("ftp://a"));
  CHECK(value_looks_like_url("www.example.com"));
  CHECK(!value_looks_like_url(""));
  CHECK(!value_looks_like_url("socketpath"));
  CHECK(!value_looks_like_url("/tmp/file"));
  CHECK(!value_looks_like_url("name:secret"));
  CHECK(!value_looks_like_url("http://"));
  CHECK(!value_looks_like_url("://x"));
  CHECK(!value_looks_like_url("X-Header: http://a"));
}

static void test_combining(void)
{
#define OPT(n) options_find(n)
  int sel[4], blocker = -1, i, j;
  /* declared conflicts, in both directions */
  CHECK(options_conflict(OPT("http2"), OPT("http1.1")));
  CHECK(options_conflict(OPT("http1.1"), OPT("http2")));
  CHECK(options_conflict(OPT("data"), OPT("form")));
  CHECK(!options_conflict(OPT("data"), OPT("header")));
  CHECK(!options_conflict(OPT("data"), OPT("data")));
  /* found by asking curl, the docs do not declare these */
  CHECK(options_conflict(OPT("continue-at"), OPT("range")));
  CHECK(options_conflict(OPT("json"), OPT("upload-file")));
  CHECK(options_conflict(OPT("data-raw"), OPT("head")));
  for(i = 0; i < g_option_count; i++)
    for(j = 0; j < g_option_count; j++)
      if(options_conflict(i, j) != options_conflict(j, i))
        CHECK(!"conflict is not symmetric");

  CHECK(options_proto_mask(OPT("user-agent")) != 0);
  CHECK(options_proto_mask(OPT("cacert")) == 0);       /* TLS: anything */
  CHECK(options_proto_mask(OPT("verbose")) == 0);      /* none listed */
  CHECK(options_proto_mask(OPT("user-agent")) == options_proto_mask(OPT("referer")));

  /* nothing chosen: everything goes */
  for(i = 0; i < g_option_count; i++)
    CHECK(options_can_combine(i, NULL, 0, NULL));

  /* first choice narrows, second narrows further */
  sel[0] = OPT("http2");
  CHECK(!options_can_combine(OPT("http1.1"), sel, 1, &blocker));
  CHECK(blocker == OPT("http2"));
  CHECK(options_can_combine(OPT("user-agent"), sel, 1, NULL));
  CHECK(options_can_combine(OPT("cacert"), sel, 1, NULL));
  CHECK(!options_can_combine(OPT("ftp-pasv"), sel, 1, NULL));
  CHECK(options_can_combine(OPT("http2"), sel, 1, NULL));  /* itself */

  /* --head is HTTP, FTP or FILE; with --ftp-pasv only FTP is left */
  sel[0] = OPT("head");
  CHECK(options_can_combine(OPT("ftp-pasv"), sel, 1, NULL));
  CHECK(options_can_combine(OPT("user-agent"), sel, 1, NULL));
  sel[1] = OPT("ftp-pasv");
  CHECK(!options_can_combine(OPT("user-agent"), sel, 2, &blocker));
  CHECK(blocker == OPT("ftp-pasv"));
  CHECK(options_can_combine(OPT("verbose"), sel, 2, NULL));
  CHECK(options_can_combine(OPT("insecure"), sel, 2, NULL));
  /* a wide option chosen later does not widen what is left */
  sel[0] = OPT("ftp-pasv");
  sel[1] = OPT("head");
  CHECK(!options_can_combine(OPT("user-agent"), sel, 2, NULL));
#undef OPT
}

static void test_win_quoting_rules(void)
{
  const char *a1[] = { "p", "plain", "with space", "", "q\"uote", "tail\\", "sp \\", "a\\\\\"b", NULL };
  char *cmd = proc_win_cmdline(a1);
  CHECK_STR(cmd, "p plain \"with space\" \"\" \"q\\\"uote\" tail\\ \"sp \\\\\" \"a\\\\\\\\\\\"b\"");
  free(cmd);
}

typedef struct {
  os_mutex lock;
  char out[1 << 16];
  size_t nout;
  size_t nerr;
} cap_t;

static void sink_out(void *u, const char *d, size_t n)
{
  cap_t *c = u;
  os_mutex_lock(&c->lock);
  if(c->nout + n < sizeof(c->out) - 1) {
    memcpy(c->out + c->nout, d, n);
    c->nout += n;
    c->out[c->nout] = 0;
  }
  os_mutex_unlock(&c->lock);
}

static void sink_err(void *u, const char *d, size_t n)
{
  cap_t *c = u;
  (void)d;
  os_mutex_lock(&c->lock);
  c->nerr += n;
  os_mutex_unlock(&c->lock);
}

static cap_t g_cap;

static void cap_reset(void)
{
  g_cap.nout = 0;
  g_cap.nerr = 0;
  g_cap.out[0] = 0;
}

static void test_process(void)
{
  char err[256];
  /* the hard quoting cases survive a real process start */
  const char *argv[] = { g_echoargs, "plain", "with space", "", "q\"uote", "tail\\",
                         "sp \\", "a\\\\\"b", "$HOME `x` 'y' %PATH% *", "\xCE\xB1\xCE\xB2", NULL };
  proc_t *p;
  const char *bad[] = { "/nonexistent/program-xyz", NULL };
  int code;

  os_mutex_init(&g_cap.lock);
  cap_reset();
  p = proc_start(argv, sink_out, sink_err, &g_cap, err, sizeof(err));
  CHECK(p != NULL);
  if(p) {
    code = proc_wait(p);
    CHECK(code == 7);
    CHECK(proc_wait(p) == 7);
    CHECK(proc_finished(p));
    proc_free(p);
    CHECK_STR(g_cap.out, "[plain]\n[with space]\n[]\n[q\"uote]\n[tail\\]\n[sp \\]\n"
                         "[a\\\\\"b]\n[$HOME `x` 'y' %PATH% *]\n[\xCE\xB1\xCE\xB2]\n");
  }

  /* a missing program either fails to spawn, or exits non-zero */
  cap_reset();
  p = proc_start(bad, sink_out, sink_err, &g_cap, err, sizeof(err));
  if(p) {
    CHECK(proc_wait(p) != 0);
    proc_free(p);
  }
  else
    CHECK(err[0] != 0);

  /* kill */
  {
    const char *sl[] = { g_echoargs, "--sleep", NULL };
    int waited = 0;
    cap_reset();
    p = proc_start(sl, sink_out, sink_err, &g_cap, err, sizeof(err));
    CHECK(p != NULL);
    if(p) {
      while(waited < 5000) {
        size_t have;
        os_mutex_lock(&g_cap.lock);
        have = g_cap.nout;
        os_mutex_unlock(&g_cap.lock);
        if(have)
          break;
        nap(5);
        waited += 5;
      }
      CHECK(!proc_finished(p));
      proc_kill(p);
      code = proc_wait(p);
      CHECK(code != 0 && code != 7);
      proc_free(p);
    }
  }
  os_mutex_destroy(&g_cap.lock);
}

static void test_outbuf(void)
{
  outbuf *b = outbuf_new(0);
  static char line[8192];
  size_t i;
  char *joined;

  CHECK(outbuf_line_count(b) == 0);
  outbuf_append(b, "one\ntwo\r\nthree", 14);
  CHECK(outbuf_line_count(b) == 3);
  outbuf_get_line(b, 0, line, sizeof(line));
  CHECK_STR(line, "one");
  outbuf_get_line(b, 1, line, sizeof(line));
  CHECK_STR(line, "two");
  outbuf_get_line(b, 2, line, sizeof(line));
  CHECK_STR(line, "three");
  /* a line continues across appends */
  outbuf_append(b, "-more\n", 6);
  CHECK(outbuf_line_count(b) == 3);
  outbuf_get_line(b, 2, line, sizeof(line));
  CHECK_STR(line, "three-more");
  joined = outbuf_join(b, NULL);
  CHECK_STR(joined, "one\ntwo\nthree-more");
  free(joined);
  CHECK(outbuf_total_in(b) == 20);

  /* lone \r rewrites the line; \r\n split across appends is one newline */
  outbuf_clear(b);
  outbuf_append(b, "50%\r100%\n", 9);
  CHECK(outbuf_line_count(b) == 1);
  outbuf_get_line(b, 0, line, sizeof(line));
  CHECK_STR(line, "100%");
  outbuf_clear(b);
  outbuf_append(b, "a\r", 2);
  outbuf_append(b, "\nb", 2);
  CHECK(outbuf_line_count(b) == 2);
  outbuf_get_line(b, 0, line, sizeof(line));
  CHECK_STR(line, "a");

  /* empty lines are kept */
  outbuf_clear(b);
  outbuf_append(b, "a\n\nb\n", 5);
  CHECK(outbuf_line_count(b) == 3);
  outbuf_get_line(b, 1, line, sizeof(line));
  CHECK_STR(line, "");

  /* control characters, tabs, invalid UTF-8 */
  outbuf_clear(b);
  outbuf_append(b, "x\x01y\tz\xFFw\xC0\xAFq\x7F", 11);
  outbuf_get_line(b, 0, line, sizeof(line));
  CHECK_STR(line, "x\xC2\xB7y    z\xC2\xB7w\xC2\xB7\xC2\xB7q\xC2\xB7");

  /* a UTF-8 character split between appends stays whole */
  outbuf_clear(b);
  outbuf_append(b, "\xE2\x82", 2);
  outbuf_append(b, "\xAC!", 2);
  outbuf_get_line(b, 0, line, sizeof(line));
  CHECK_STR(line, "\xE2\x82\xAC!");

  /* long lines split without cutting a character */
  outbuf_clear(b);
  for(i = 0; i < 5000; i++)
    outbuf_append(b, "\xCE\xB1", 2);
  CHECK(outbuf_line_count(b) == 3);
  outbuf_lock(b);
  for(i = 0; i < outbuf_line_count_locked(b); i++) {
    size_t len, k;
    const char *s = outbuf_line_locked(b, i, &len);
    CHECK(len <= OUTBUF_MAX_LINE);
    CHECK(len % 2 == 0);
    for(k = 0; k < len; k += 2)
      CHECK((unsigned char)s[k] == 0xCE);
  }
  outbuf_unlock(b);

  /* markers leave the text and do not disturb neighbours */
  outbuf_clear(b);
  outbuf_set_marker(b, "@@m ");
  CHECK(!outbuf_get_marker(b, line, sizeof(line)));
  outbuf_append(b, "before\n@@m http=200\nafter\n", 26);
  CHECK(outbuf_line_count(b) == 2);
  outbuf_get_line(b, 0, line, sizeof(line));
  CHECK_STR(line, "before");
  outbuf_get_line(b, 1, line, sizeof(line));
  CHECK_STR(line, "after");
  CHECK(outbuf_get_marker(b, line, sizeof(line)));
  CHECK_STR(line, "http=200");
  outbuf_set_marker(b, NULL);
  outbuf_free(b);

  /* the cap is enforced */
  b = outbuf_new(1000);
  for(i = 0; i < 500; i++)
    outbuf_append(b, "0123456789\n", 11);
  CHECK(outbuf_truncated(b));
  CHECK(outbuf_total_in(b) == 5500);
  joined = outbuf_join(b, &i);
  CHECK(i <= 1000 + outbuf_line_count(b));
  free(joined);
  outbuf_free(b);
}

typedef struct {
  outbuf *b;
  int id;
} writer_arg;

static OS_THREAD_FN writer(void *arg)
{
  writer_arg *w = arg;
  int i;
  char line[32];
  for(i = 0; i < 2000; i++) {
    int n = snprintf(line, sizeof(line), "t%d line %d\n", w->id, i);
    outbuf_append(w->b, line, (size_t)n);
  }
  return OS_THREAD_RET;
}

static void test_outbuf_threads(void)
{
  outbuf *b = outbuf_new(0);
  os_thread t[4];
  writer_arg a[4];
  int i;
  for(i = 0; i < 4; i++) {
    a[i].b = b;
    a[i].id = i;
    CHECK(os_thread_start(&t[i], writer, &a[i]) == 0);
  }
  for(i = 0; i < 2000; i++) {
    char tmp[64];
    size_t n = outbuf_line_count(b);
    if(n)
      outbuf_get_line(b, n - 1, tmp, sizeof(tmp));
  }
  for(i = 0; i < 4; i++)
    os_thread_join(t[i]);
  CHECK(outbuf_line_count(b) == 8000);
  outbuf_free(b);
}

static void json_ok(const char *in, const char *want)
{
  char *out = json_pretty(in, strlen(in), NULL);
  CHECK_STR(out, want);
  free(out);
}

static void json_bad(const char *in)
{
  char *out = json_pretty(in, strlen(in), NULL);
  if(out)
    printf("  accepted invalid: %s\n", in);
  CHECK(out == NULL);
  free(out);
}

static void test_json(void)
{
  char *deep, *out;
  json_ok("1", "1");
  json_ok(" \"x\" ", "\"x\"");
  json_ok("null", "null");
  json_ok("{}", "{}");
  json_ok("[ ]", "[]");
  json_ok("{\"a\":1}", "{\n  \"a\": 1\n}");
  json_ok("{\"a\":{\"b\":[1,2,3]},\"c\":\"d\"}",
          "{\n  \"a\": {\n    \"b\": [1, 2, 3]\n  },\n  \"c\": \"d\"\n}");
  json_ok("[{\"a\":1},2]", "[\n  {\n    \"a\": 1\n  },\n  2\n]");
  json_ok("[1,[2]]", "[\n  1,\n  [2]\n]");
  json_ok("[\"\xCE\xB1\", true, null, -1.5e3]", "[\"\xCE\xB1\", true, null, -1.5e3]");
  json_ok("\"a\\u00e9\\n\\\"\"", "\"a\\u00e9\\n\\\"\"");
  /* long scalar arrays are not inlined */
  json_ok("[\"0123456789012345678901234567890\",\"0123456789012345678901234567890\"]",
          "[\n  \"0123456789012345678901234567890\",\n  \"0123456789012345678901234567890\"\n]");

  json_bad("");
  json_bad("   ");
  json_bad("{");
  json_bad("}");
  json_bad("[1,]");
  json_bad("[,1]");
  json_bad("{\"a\"}");
  json_bad("{\"a\":}");
  json_bad("{\"a\":1,}");
  json_bad("{a:1}");
  json_bad("[1 2]");
  json_bad("tru");
  json_bad("nul");
  json_bad("01");
  json_bad("1.");
  json_bad(".5");
  json_bad("-");
  json_bad("1e");
  json_bad("+1");
  json_bad("\"abc");
  json_bad("\"a\\x\"");
  json_bad("\"\\u12g4\"");
  json_bad("\"a\nb\"");
  json_bad("1 2");
  json_bad("{} {}");
  json_bad("<html></html>");
  json_bad("[1,2,3");       /* inline probe must not accept this */
  json_bad("[\"a\",]");

  /* depth limit: no stack blowup, rejected */
  deep = malloc(200003);
  memset(deep, '[', 100000);
  memset(deep + 100000, ']', 100000);
  deep[200000] = 0;
  CHECK(json_pretty(deep, 200000, NULL) == NULL);
  /* just inside the limit is fine */
  memset(deep, '[', 100);
  memset(deep + 100, ']', 100);
  deep[200] = 0;
  out = json_pretty(deep, 200, NULL);
  CHECK(out != NULL);
  free(out);
  free(deep);
  /* a NUL inside the data is invalid, not an end */
  CHECK(json_pretty("1\0", 2, NULL) == NULL);
  CHECK(json_pretty("\"a\0b\"", 5, NULL) == NULL);
  CHECK(json_pretty("\"\\\0\"", 4, NULL) == NULL);
}

static void test_curlinfo_parse(void)
{
  curl_info ci;
  memset(&ci, 0, sizeof(ci));
  curlinfo_parse_version("curl 8.18.0 (x86_64-redhat-linux-gnu) libcurl/8.18.0\nRelease-Date: x\n", &ci);
  CHECK(ci.found);
  CHECK_STR(ci.version, "8.18.0");
  CHECK(ci.major == 8 && ci.minor == 18 && ci.patch == 0);
  CHECK_STR(ci.banner, "curl 8.18.0 (x86_64-redhat-linux-gnu) libcurl/8.18.0");
  CHECK(curlinfo_at_least(&ci, 7, 63));
  CHECK(curlinfo_at_least(&ci, 8, 18));
  CHECK(!curlinfo_at_least(&ci, 8, 19));

  /* too few names to trust: everything counts as supported */
  CHECK(curlinfo_supports(&ci, "whatever"));

  {
    char help[4096] = "Usage: curl [options...] <url>\n"
      "     --abstract-unix-socket <path>   Connect via socket\n"
      " -A, --user-agent <name>   Send User-Agent\n"
      " -:, --next   Next\n"
      " -#, --progress-bar   Bar\n";
    int i;
    for(i = 0; i < 30; i++) {
      char l[64];
      snprintf(l, sizeof(l), "     --opt%02d  Thing\n", i);
      strcat(help, l);
    }
    curlinfo_parse_help(help, &ci);
    CHECK(curlinfo_supports(&ci, "user-agent"));
    CHECK(curlinfo_supports(&ci, "abstract-unix-socket"));
    CHECK(curlinfo_supports(&ci, "next"));
    CHECK(curlinfo_supports(&ci, "progress-bar"));
    CHECK(!curlinfo_supports(&ci, "http4"));
  }
  curlinfo_free(&ci);

  memset(&ci, 0, sizeof(ci));
  curlinfo_parse_version("not curl at all\n", &ci);
  CHECK(!ci.found);
  CHECK(curlinfo_supports(&ci, "anything"));
}

static void test_docs(void)
{
  doc_t d;
  CHECK(docs_parse("Use --location and `code` and **bold** text.\n"
                   "Second line --nothere-nope.\n\n"
                   "## value\nDescribes it\n\n"
                   "    code --location\n\n    more\n\n"
                   "- bullet one\n- bullet\n  two\n", &d) == 0);
  CHECK(d.nblocks == 6);
  if(d.nblocks == 6) {
    doc_block *p = &d.blocks[0];
    CHECK(p->kind == BLOCK_PARA);
    CHECK(p->nspans >= 6);
    if(p->nspans >= 6) {
      CHECK(p->spans[0].kind == SPAN_TEXT);
      CHECK_STR(p->spans[0].text, "Use ");
      CHECK(p->spans[1].kind == SPAN_LINK);
      CHECK_STR(p->spans[1].text, "--location");
      CHECK(p->spans[1].opt == options_find("location"));
      CHECK(p->spans[3].kind == SPAN_CODE);
      CHECK_STR(p->spans[3].text, "code");
      CHECK(p->spans[5].kind == SPAN_BOLD);
      CHECK_STR(p->spans[5].text, "bold");
    }
    CHECK(d.blocks[1].kind == BLOCK_TAG);
    CHECK_STR(d.blocks[1].spans[0].text, "value");
    CHECK(d.blocks[2].kind == BLOCK_PARA);
    CHECK(d.blocks[3].kind == BLOCK_CODE);
    CHECK_STR(d.blocks[3].spans[0].text, "code --location\n\nmore");
    CHECK(d.blocks[4].kind == BLOCK_BULLET);
    CHECK(d.blocks[5].kind == BLOCK_BULLET);
    CHECK_STR(d.blocks[5].spans[0].text, "bullet two");
  }
  docs_free(&d);

  docs_parse("Escapes \\<file\\> and --location.\n", &d);
  CHECK(d.nblocks == 1);
  CHECK_STR(d.blocks[0].spans[0].text, "Escapes <file> and ");
  CHECK_STR(d.blocks[0].spans[1].text, "--location");
  CHECK_STR(d.blocks[0].spans[2].text, ".");
  docs_free(&d);

  /* every real doc parses, with sane link targets */
  {
    int i, j, k, links = 0;
    for(i = 0; i < g_option_count; i++) {
      CHECK(docs_parse(g_options[i].description, &d) == 0);
      CHECK(d.nblocks > 0);
      for(j = 0; j < d.nblocks; j++)
        for(k = 0; k < d.blocks[j].nspans; k++)
          if(d.blocks[j].spans[k].kind == SPAN_LINK) {
            CHECK(d.blocks[j].spans[k].opt >= 0);
            links++;
          }
      docs_free(&d);
    }
    CHECK(links > 200);
  }
}

static void sink_outbuf(void *u, const char *d, size_t n)
{
  outbuf_append(u, d, n);
}

static void sink_drop(void *u, const char *d, size_t n)
{
  (void)u;
  (void)d;
  (void)n;
}

static void test_settings(void)
{
  settings_t s, l;
  request r, q;
  int method = 0;
  char path[512], rpath[512], tmpname[64];
  const char *base = getenv("TEMP");
  FILE *f;
  arg_list args;
  char *prev;

  settings_defaults(&s);
  CHECK(s.dark == 1 && s.zoom == 0 && s.pretty == 1 && s.capture == 1 && s.hover_ms == 110);
  CHECK_STR(s.program, "curl");

  snprintf(tmpname, sizeof(tmpname), "curlui-test-%ld", (long)time(NULL));
#ifdef _WIN32
  snprintf(path, sizeof(path), "%s\\%s\\sub\\settings.ini", base ? base : ".", tmpname);
  snprintf(rpath, sizeof(rpath), "%s\\%s\\sub\\req.txt", base ? base : ".", tmpname);
#else
  (void)base;
  snprintf(path, sizeof(path), "/tmp/%s/sub/settings.ini", tmpname);
  snprintf(rpath, sizeof(rpath), "/tmp/%s/sub/req.txt", tmpname);
#endif
  CHECK(settings_path("/x", "a.ini", tmpname, sizeof(tmpname)) == 0);
#ifndef _WIN32
  CHECK_STR(tmpname, "/x/a.ini");
#endif

  /* a missing file leaves the defaults */
  settings_defaults(&l);
  CHECK(settings_load(&l, path) == 0);
  CHECK(l.dark == 1);

  /* round trip, with characters that need escaping; directories are created */
  s.dark = 0;
  s.zoom = 125;
  snprintf(s.program, sizeof(s.program), "C:\\Program Files\\curl\nodd");
  snprintf(s.config_file, sizeof(s.config_file), "/home/me/my curl.conf");
  s.no_curlrc = 1;
  s.pretty = 0;
  s.capture = 0;
  s.hover_ms = 300;
  s.remember = 1;
  s.win_w = 1500;
  s.win_h = 900;
  CHECK(settings_save(&s, path) == 0);
  settings_defaults(&l);
  CHECK(settings_load(&l, path) == 1);
  CHECK(l.dark == 0 && l.zoom == 125 && l.no_curlrc == 1 && l.pretty == 0 && l.capture == 0);
  CHECK(l.hover_ms == 300 && l.remember == 1 && l.win_w == 1500 && l.win_h == 900);
  CHECK_STR(l.program, s.program);
  CHECK_STR(l.config_file, s.config_file);
#ifndef _WIN32
  {
    /* it can name a config holding secrets: not world readable */
    char cmd[1200];
    FILE *p;
    int mode = 0;
    snprintf(cmd, sizeof(cmd), "stat -c %%a '%s' 2>/dev/null || stat -f %%Lp '%s'", path, path);
    p = popen(cmd, "r");
    if(p) {
      if(fscanf(p, "%d", &mode) != 1)
        mode = 0;
      pclose(p);
      CHECK(mode == 600);
    }
  }
#endif

  /* a hand-edited file: junk, comments and bad values keep the defaults */
  f = fopen(path, "wb");
  CHECK(f != NULL);
  if(f) {
    fputs("# comment\n\nnot a pair\n=x\ntheme=light\r\ntext_size=9999\npretty_json=maybe\n"
          "docs_hover_ms=-5\ncurl_program=\nunknown_key=1\nwindow_width=abc\n", f);
    fclose(f);
  }
  settings_defaults(&l);
  CHECK(settings_load(&l, path) == 1);
  CHECK(l.dark == 0);
  CHECK(l.zoom == 0);
  CHECK(l.pretty == 1);
  CHECK(l.hover_ms == 110);
  CHECK_STR(l.program, "curl");
  CHECK(l.win_w == 0);

  /* the request round trip */
  request_init(&r);
  request_set_url(&r, "https://example.com/a b");
  request_set_value(request_add(&r, options_find("header")), 0, "X-A: 1");
  request_append_value(request_add(&r, options_find("header")), "X: two\nlines\\");
  request_add(&r, options_find("location"))->negated = 1;
  request_add(&r, options_find("referer"));
  CHECK(request_save(&r, 2, rpath) == 0);
  request_init(&q);
  CHECK(request_load(&q, &method, rpath) == 1);
  CHECK(method == 2);
  CHECK_STR(q.url, r.url);
  CHECK(q.nsel == 3);
  if(q.nsel == 3) {
    CHECK(q.sel[0].nvalues == 2);
    CHECK_STR(q.sel[0].values[0], "X-A: 1");
    CHECK_STR(q.sel[0].values[1], "X: two\nlines\\");
    CHECK(q.sel[1].negated == 1);
    CHECK(q.sel[2].nvalues == 1 && q.sel[2].values[0][0] == 0);
  }
  request_free(&q);
  /* unknown options and out of range methods from an old or edited file */
  f = fopen(rpath, "wb");
  if(f) {
    fputs("method=99\noption=no-such-option\nvalue=x\noption=location\nvalue=ignored\nnegated=1\n", f);
    fclose(f);
  }
  request_init(&q);
  CHECK(request_load(&q, &method, rpath) == 1);
  CHECK(method == 0);
  CHECK(q.nsel == 1 && q.sel[0].negated == 1 && q.sel[0].nvalues == 0);
  request_free(&q);
  request_free(&r);
  remove(rpath);
  remove(path);

  /* --disable must be first or curl ignores it, and --config follows it */
  request_init(&r);
  request_set_url(&r, "http://x/");
  request_add(&r, options_find("location"));
  request_add(&r, options_find("disable"));
  r.config_file = "my.conf";
  CHECK(builder_build(&r, "curl", &args) == 0);
  prev = builder_preview(&args);
  CHECK_STR(prev, "curl --disable --config my.conf --location http://x/");
  free(prev);
  arg_list_free(&args);
  request_remove(&r, options_find("disable"));
  r.no_curlrc = 1;
  r.config_file = NULL;
  CHECK(builder_build(&r, "curl", &args) == 0);
  prev = builder_preview(&args);
  CHECK_STR(prev, "curl --disable --location http://x/");
  free(prev);
  arg_list_free(&args);
  request_free(&r);
}

/* End to end: real curl, real file:// transfer, through builder + runner +
   output buffer, the same path the UI uses. */
static void test_e2e(void)
{
#ifdef _WIN32
  const char *tmpdir = getenv("TEMP");
#else
  const char *tmpdir = "/tmp";
#endif
  char path[512], err[256], enc[1024];
  request r;
  arg_list l;
  const char **argv;
  proc_t *p;
  FILE *f;
  outbuf *o;
  curl_info ci;
  char line[256];
  size_t i, n = 0;

  if(!g_curl) {
    printf("e2e: skipped, no curl given\n");
    return;
  }
  CHECK(curlinfo_detect(g_curl, &ci));
  if(!ci.found) {
    printf("e2e: curl did not run: %s\n", g_curl);
    return;
  }
  CHECK(ci.nnames > 100);
  CHECK(curlinfo_supports(&ci, "user-agent"));
  CHECK(!curlinfo_supports(&ci, "not-a-real-option"));
  curlinfo_free(&ci);

  snprintf(path, sizeof(path), "%s/curlui e2e %ld.txt", tmpdir ? tmpdir : ".", (long)time(NULL));
  f = fopen(path, "wb");
  CHECK(f != NULL);
  if(!f)
    return;
  fputs("{\"hello\":\"file transfer\"}\nsecond\n", f);
  fclose(f);

  request_init(&r);
  /* a space is not valid in a URL, and Windows paths need forward slashes */
  n = (size_t)snprintf(enc, sizeof(enc), "file:///");
  for(i = (path[0] == '/') ? 1 : 0; path[i] && n + 4 < sizeof(enc); i++) {
    char c = path[i] == '\\' ? '/' : path[i];
    if(c == ' ') {
      memcpy(enc + n, "%20", 3);
      n += 3;
    }
    else
      enc[n++] = c;
  }
  enc[n] = 0;
  request_set_url(&r, enc);
  request_add(&r, options_find("silent"));
  CHECK(builder_build(&r, g_curl, &l) == 0);
  argv = arg_list_argv(&l, NULL);
  o = outbuf_new(0);
  p = proc_start(argv, sink_outbuf, sink_drop, o, err, sizeof(err));
  CHECK(p != NULL);
  if(p) {
    int code = proc_wait(p);
    CHECK(code == 0);
    proc_free(p);
    CHECK(outbuf_line_count(o) == 2);
    outbuf_get_line(o, 1, line, sizeof(line));
    CHECK_STR(line, "second");
    outbuf_get_line(o, 0, line, sizeof(line));
    {
      char *pretty = json_pretty(line, strlen(line), NULL);
      CHECK_STR(pretty, "{\n  \"hello\": \"file transfer\"\n}");
      free(pretty);
    }
  }
  free((void *)argv);
  arg_list_free(&l);
  outbuf_free(o);
  request_free(&r);
  remove(path);
}

int main(int argc, char **argv)
{
  g_echoargs = argc > 1 ? argv[1] : NULL;
  g_curl = argc > 2 ? argv[2] : NULL;
  test_options();
  test_builder();
  test_url_checks();
  test_combining();
  test_win_quoting_rules();
  test_outbuf();
  test_outbuf_threads();
  test_json();
  test_curlinfo_parse();
  test_docs();
  test_settings();
  if(g_echoargs)
    test_process();
  test_e2e();
  printf("%d checks, %d failed\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
