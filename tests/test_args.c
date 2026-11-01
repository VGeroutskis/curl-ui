/* Every option in the table, through the real builder and process runner.
   usage: test_args <echoargs> [curl] [--verbose]

   1. What is built reaches the child byte for byte, hard values included.
   2. A real curl parses it. `curl OPTION [VALUE] --version` stops at --version
      after parsing what came before, so nothing is transferred. */
#include "builder.h"
#include "curlinfo.h"
#include "options.h"
#include "osthread.h"
#include "procrun.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail, g_checks, g_verbose;

typedef struct {
  char out[1 << 16];
  size_t nout;
  char err[1 << 14];
  size_t nerr;
  os_mutex lock;
} cap_t;

static cap_t g_cap;

static void on_out(void *u, const char *d, size_t n)
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

static void on_err(void *u, const char *d, size_t n)
{
  cap_t *c = u;
  os_mutex_lock(&c->lock);
  if(c->nerr + n < sizeof(c->err) - 1) {
    memcpy(c->err + c->nerr, d, n);
    c->nerr += n;
    c->err[c->nerr] = 0;
  }
  os_mutex_unlock(&c->lock);
}

/* Runs argv, returns the exit code, or -1 if it could not start. */
static int run(const char *const *argv)
{
  char err[256];
  proc_t *p;
  int code;
  g_cap.nout = g_cap.nerr = 0;
  g_cap.out[0] = g_cap.err[0] = 0;
  p = proc_start(argv, on_out, on_err, &g_cap, err, sizeof(err));
  if(!p)
    return -1;
  code = proc_wait(p);
  proc_free(p);
  return code;
}

static void fail(const char *what, const char *name, const char *detail)
{
  g_fail++;
  printf("FAIL %s --%s: %s\n", what, name, detail);
}

#define FIXTURE "curlui-args-fixture.txt"
#define SCRATCH "curlui-args-scratch.txt"

/* A value curl's own docs use for this option, else one that fits its type. */
static const char *good_value(const opt_def *o)
{
  static const struct { const char *name, *value; } fixed[] = {
    /* the docs' example needs something the parser rejects on its own, or
       has no example of the option's value */
    { "url-query", "a=b" },
    { "data-urlencode", "a=b" },
    { "form", "a=b" },
    { "form-string", "a=b" },
    { "ciphers", "ECDHE-RSA-AES128-GCM-SHA256" },
    /* curl checks at parse time that these exist */
    { "cacert", FIXTURE }, { "proxy-cacert", FIXTURE }, { "crlfile", FIXTURE },
    { "proxy-crlfile", FIXTURE }, { "knownhosts", FIXTURE }, { "netrc-file", FIXTURE },
    { "config", FIXTURE },
    { "data-ascii", "@" FIXTURE }, { "data-binary", "@" FIXTURE }, { "url", "@" FIXTURE },
    /* --stderr opens its file during the parse */
    { "stderr", SCRATCH },
    { "limit-rate", "100K" },
    { "upload-flags", "answered,seen" },
    { NULL, NULL }
  };
  int i;
  for(i = 0; fixed[i].name; i++)
    if(!strcmp(fixed[i].name, o->name))
      return fixed[i].value;
  if(!strncmp(o->placeholder, "e.g. ", 5))
    return o->placeholder + 5;
  return "1";
}

/* --option value and --no-x all survive exactly, whatever the value holds. */
static void check_argv(const char *echoargs)
{
  static const char *const hard[] = {
    "plain", "with space", "q\"uote", "it's", "tail\\", "sp \\", "$HOME `x` %PATH% *",
    "a=b&c=d", "\xCE\xB1\xCE\xB2\xCE\xB3 \xD0\x96", "-looks-like-a-flag", "--also", NULL
  };
  int i, h;
  for(i = 0; i < g_option_count; i++) {
    const opt_def *o = &g_options[i];
    int forms = o->arg[0] ? 0 : (o->neg[0] ? 2 : 1);
    if(o->arg[0]) {
      for(h = 0; hard[h]; h++) {
        request r;
        arg_list l;
        const char **argv;
        char want[1024], flag[160];
        sel_option *s;
        request_init(&r);
        s = request_add(&r, i);
        snprintf(s->values[0], BUILDER_VALUE_CAP, "%s", hard[h]);
        if(o->repeatable)
          snprintf(request_add_value(s), BUILDER_VALUE_CAP, "second %s", hard[h]);
        snprintf(r.url, sizeof(r.url), "http://u/");
        builder_build(&r, echoargs, &l);
        argv = arg_list_argv(&l, NULL);
        snprintf(flag, sizeof(flag), "--%s", o->name);
        if(o->repeatable)
          snprintf(want, sizeof(want), "[%s]\n[%s]\n[%s]\n[second %s]\n[http://u/]\n", flag, hard[h], flag, hard[h]);
        else
          snprintf(want, sizeof(want), "[%s]\n[%s]\n[http://u/]\n", flag, hard[h]);
        g_checks++;
        if(run(argv) != 7 || strcmp(g_cap.out, want))
          fail("argv", o->name, g_cap.out);
        free((void *)argv);
        arg_list_free(&l);
        request_free(&r);
      }
    }
    for(h = 0; h < forms; h++) {
      request r;
      arg_list l;
      const char **argv;
      char want[256];
      sel_option *s;
      request_init(&r);
      s = request_add(&r, i);
      s->negated = h;
      snprintf(r.url, sizeof(r.url), "http://u/");
      builder_build(&r, echoargs, &l);
      argv = arg_list_argv(&l, NULL);
      snprintf(want, sizeof(want), "[--%s]\n[http://u/]\n", h ? o->neg : o->name);
      g_checks++;
      if(run(argv) != 7 || strcmp(g_cap.out, want))
        fail("argv", o->name, g_cap.out);
      free((void *)argv);
      arg_list_free(&l);
      request_free(&r);
    }
  }
}

/* Options that end the run themselves, so reaching them proves the parse. */
static int exits_by_itself(const opt_def *o)
{
  return !strcmp(o->name, "help") || !strcmp(o->name, "manual") || !strcmp(o->name, "version") ||
         !strcmp(o->name, "dump-ca-embed") || !strcmp(o->name, "engine");
}

/* Rejected at parse time is curl's exit code 2 with a message about the option.
   Features this build lacks are real answers about that build, not our fault. */
static int feature_missing(const char *err)
{
  return strstr(err, "not support") || strstr(err, "doesn't support") || strstr(err, "does not support") ||
         strstr(err, "disabled") || strstr(err, "not built") || strstr(err, "was built without");
}

static void check_curl_form(const char *curl, const curl_info *ci, const opt_def *o, int negated,
                            int *accepted, int *not_in_curl, int *no_feature)
{
  const char *value = o->arg[0] ? good_value(o) : NULL;
  const char *flagname = negated ? o->neg : o->name;
  const char *argv[7];
  char flag[160];
  int n = 0, code;
  snprintf(flag, sizeof(flag), "--%s", flagname);
  argv[n++] = curl;
  if(!strcmp(o->name, "next"))
    argv[n++] = "http://localhost/";  /* it separates URLs, so it needs one */
  argv[n++] = flag;
  if(value)
    argv[n++] = value;
  argv[n++] = "--version";
  argv[n] = NULL;
  g_checks++;
  code = run(argv);
  if(code == 0 && (strstr(g_cap.out, "curl ") || exits_by_itself(o))) {
    (*accepted)++;
    if(g_verbose)
      printf("ok   %s %s\n", flag, value ? value : "");
    return;
  }
  if(strstr(g_cap.err, "is unknown") || !curlinfo_supports(ci, o->name)) {
    (*not_in_curl)++;
    printf("note --%s is not in this curl\n", flagname);
    return;
  }
  if(feature_missing(g_cap.err)) {
    (*no_feature)++;
    printf("note --%s needs a feature this curl lacks\n", flagname);
    return;
  }
  if(exits_by_itself(o)) {
    (*accepted)++;
    return;
  }
  /* some options only gained their --no- form in later versions */
  if(negated && strstr(g_cap.err, "isn't a boolean") && !curlinfo_at_least(ci, 8, 0)) {
    (*not_in_curl)++;
    printf("note --%s is not in this older curl\n", flagname);
    return;
  }
  {
    char detail[400];
    snprintf(detail, sizeof(detail), "value [%s] exit %d: %.300s", value ? value : "", code, g_cap.err);
    fail("curl", flagname, detail);
  }
}

static void check_curl(const char *curl)
{
  curl_info ci;
  int i, accepted = 0, not_in_curl = 0, no_feature = 0;
  if(!curlinfo_detect(curl, &ci)) {
    fail("curl", "(detect)", curl);
    return;
  }
  {
    FILE *f = fopen(FIXTURE, "wb");
    if(f)
      fclose(f);
  }
  for(i = 0; i < g_option_count; i++) {
    const opt_def *o = &g_options[i];
    check_curl_form(curl, &ci, o, 0, &accepted, &not_in_curl, &no_feature);
    if(o->neg[0])
      check_curl_form(curl, &ci, o, 1, &accepted, &not_in_curl, &no_feature);
  }
  remove(FIXTURE);
  remove(SCRATCH);
  printf("%s: %d forms accepted, %d not in this curl, %d need a missing feature\n", ci.banner, accepted, not_in_curl,
         no_feature);
  curlinfo_free(&ci);
}

int main(int argc, char **argv)
{
  const char *echoargs = argc > 1 ? argv[1] : NULL;
  const char *curl = (argc > 2 && strcmp(argv[2], "--verbose")) ? argv[2] : NULL;
  int i;
  for(i = 1; i < argc; i++)
    if(!strcmp(argv[i], "--verbose"))
      g_verbose = 1;
  if(!echoargs) {
    fputs("usage: test_args <echoargs> [curl] [--verbose]\n", stderr);
    return 2;
  }
  os_mutex_init(&g_cap.lock);
  check_argv(echoargs);
  if(curl)
    check_curl(curl);
  os_mutex_destroy(&g_cap.lock);
  printf("%d checks, %d failed\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
