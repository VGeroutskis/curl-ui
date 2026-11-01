#include "app.h"
#include "argfile.h"
#include "json_fmt.h"

#include <SDL.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MARKER_PREFIX "@@curlui "

const recipe g_recipes[] = {
  { "Fetch JSON", "GET an API and pretty-print the reply", "", "https://httpbin.org/json",
    { { "header", "Accept: application/json" } } },
  { "Send JSON", "POST a JSON body with the right headers", "", "https://httpbin.org/post",
    { { "json", "{\"hello\": \"world\"}" } } },
  { "Follow redirects", "Keep going when the server says 3xx", "", "https://httpbin.org/redirect/2",
    { { "location", NULL } } },
  { "Show response headers", "Include status line and headers in the output", "", "https://example.com",
    { { "include", NULL } } },
  { "Download a file", "Save under the name in the URL", "", "https://example.com/index.html",
    { { "remote-name", NULL }, { "location", NULL } } },
  { "Basic auth", "Send a user name and password", "", "https://httpbin.org/basic-auth/name/secret",
    { { "user", "name:secret" } } },
  { "Debug a request", "See everything curl sends and receives", "", "https://example.com",
    { { "verbose", NULL } } },
  { "Upload a file", "PUT a local file to the server", "", "https://httpbin.org/put",
    { { "upload-file", "local.txt" } } },
  { "Skip TLS verification", "Connect despite a bad certificate", "", "https://self-signed.badssl.com",
    { { "insecure", NULL } } },
  { "Timeouts and retries", "Fail fast, and try again on hiccups", "", "https://example.com",
    { { "connect-timeout", "5" }, { "max-time", "30" }, { "retry", "3" } } },
};
const int g_recipe_count = (int)(sizeof(g_recipes) / sizeof(g_recipes[0]));

static const char *const method_names[METHOD_COUNT] = {
  "", "GET", "POST", "PUT", "PATCH", "DELETE", "HEAD", "OPTIONS"
};

void wrap_free(wrap_cache *w)
{
  free(w->rows);
  memset(w, 0, sizeof(*w));
}

/* With no explicit method curl picks one from the options; show what it will
   pick rather than a misleading GET. */
const char *app_method_label(const app_t *a, const tab_t *t, int index)
{
  static const char *const body_opts[] = {
    "data", "data-ascii", "data-binary", "data-raw", "data-urlencode", "json", "form",
    "form-string", NULL
  };
  int i;
  (void)a;
  if(index > 0)
    return method_names[index];
  for(i = 0; body_opts[i]; i++)
    if(request_find(&t->req, options_find(body_opts[i])) >= 0)
      return "POST";
  if(request_find(&t->req, options_find("upload-file")) >= 0)
    return "PUT";
  if(request_find(&t->req, options_find("head")) >= 0)
    return "HEAD";
  return "GET";
}

static void tab_free(tab_t *t)
{
  /* the readers write to the buffers until the process is gone */
  if(t->proc)
    proc_free(t->proc);
  args_files_remove(&t->run_files);
  arg_list_free(&t->args_tmpl);
  arg_list_free(&t->args_real);
  arg_list_free(&t->args);
  mask_free(t->mask);
  kv_free(&t->params);
  free(t->params_url);
  form_hints_free(&t->hints);
  kv_free(&t->header_rows);
  kv_free(&t->body_rows);
  free(t->body_file);
  free(t->auth_a);
  free(t->auth_b);
  outbuf_free(t->out);
  outbuf_free(t->log);
  outbuf_free(t->pretty_buf);
  wrap_free(&t->wrap_out);
  wrap_free(&t->wrap_log);
  wrap_free(&t->wrap_pretty);
  request_free(&t->req);
  free(t);
}

int tab_new(app_t *a)
{
  tab_t *t;
  if(a->ntabs == MAX_TABS)
    return -1;
  t = calloc(1, sizeof(*t));
  if(!t)
    return -1;
  request_init(&t->req);
  t->out = outbuf_new(0);
  t->log = outbuf_new(0);
  t->pretty_buf = outbuf_new(0);
  outbuf_set_marker(t->log, MARKER_PREFIX);
  t->out_follow = 1;
  t->pretty = a->set.pretty;
  a->tabs[a->ntabs] = t;
  a->cur = a->ntabs++;
  return a->cur;
}

void tab_close(app_t *a, int i)
{
  if(i < 0 || i >= a->ntabs)
    return;
  tab_free(a->tabs[i]);
  memmove(&a->tabs[i], &a->tabs[i + 1], sizeof(tab_t *) * (size_t)(a->ntabs - i - 1));
  a->ntabs--;
  if(a->cur > i || a->cur >= a->ntabs)
    a->cur = a->cur > 0 ? a->cur - 1 : 0;
  if(!a->ntabs)
    tab_new(a);
  a->pending_unfocus = 1;
}

int app_any_running(const app_t *a)
{
  int i;
  for(i = 0; i < a->ntabs; i++)
    if(a->tabs[i]->proc)
      return 1;
  return 0;
}

void app_init(app_t *a)
{
  memset(a, 0, sizeof(*a));
  settings_defaults(&a->set);
  vars_init(&a->vars);
  a->env_edit_for = -1;
  tab_new(a);
  a->results = malloc(sizeof(int) * (size_t)g_option_count);
  a->doc_opt = -1;
  a->doc_built_opt = -2;
  a->hover_cand = -1;
  a->filter_last = -1;
  a->doc_arena = malloc(8192);
}

void app_free(app_t *a)
{
  int i;
  for(i = 0; i < a->ntabs; i++)
    tab_free(a->tabs[i]);
  a->ntabs = 0;
  modal_close(a);
  vars_free(&a->vars);
  free(a->dops);
  free(a->doc_arena);
  docs_free(&a->doc);
  curlinfo_free(&a->curl);
  free(a->results);
}

void app_detect_curl(app_t *a)
{
  curlinfo_free(&a->curl);
  curlinfo_detect(a->set.program[0] ? a->set.program : "curl", &a->curl);
}

int app_option_supported(const app_t *a, int opt)
{
  return curlinfo_supports(&a->curl, g_options[opt].name);
}

void app_rebuild_args(app_t *a)
{
  arg_list_free(&CUR(a)->args_tmpl);
  arg_list_free(&CUR(a)->args_real);
  arg_list_free(&CUR(a)->args);
  CUR(a)->req.no_curlrc = a->set.no_curlrc;
  CUR(a)->req.config_file = a->set.config_file;
  snprintf(CUR(a)->req.method, sizeof(CUR(a)->req.method), "%s", method_names[CUR(a)->method]);
  if(builder_build(&CUR(a)->req, a->set.program[0] ? a->set.program : "curl", &CUR(a)->args_tmpl))
    return;
  vars_apply(&a->vars.e[a->vars.active], &CUR(a)->args_tmpl, &CUR(a)->args_real, &CUR(a)->args);
}

void app_notice(app_t *a, const char *text)
{
  snprintf(a->notice, sizeof(a->notice), "%s", text);
}

void app_add_option(app_t *a, const char *name, const char *value)
{
  int opt = options_find(name);
  sel_option *s;
  if(opt < 0)
    return;
  CUR(a)->form = FORM_CURL;
  s = request_add(&CUR(a)->req, opt);
  if(s && value && g_options[opt].arg[0]) {
    int i, slot = -1;
    for(i = 0; i < s->nvalues && slot < 0; i++)
      if(!s->values[i][0])
        slot = i;
    if(slot >= 0)
      request_set_value(s, slot, value);
    else
      request_append_value(s, value);
  }
}

void app_toggle_option(app_t *a, int opt)
{
  CUR(a)->form = FORM_CURL;
  if(request_find(&CUR(a)->req, opt) >= 0)
    request_remove(&CUR(a)->req, opt);
  else {
    request_add(&CUR(a)->req, opt);
    CUR(a)->show_recipes = 0;
  }
}

void app_apply_recipe(app_t *a, int index)
{
  CUR(a)->form = FORM_CURL;
  const recipe *r = &g_recipes[index];
  int i;
  request_clear_options(&CUR(a)->req);
  CUR(a)->method = 0;
  request_set_url(&CUR(a)->req, r->url);
  for(i = 0; i < 5 && r->opts[i].name; i++)
    app_add_option(a, r->opts[i].name, r->opts[i].value);
  CUR(a)->show_recipes = 0;
}

void app_clear_output(app_t *a)
{
  outbuf_clear(CUR(a)->out);
  outbuf_clear(CUR(a)->log);
  outbuf_clear(CUR(a)->pretty_buf);
  CUR(a)->has_result = 0;
  CUR(a)->has_stats = 0;
  CUR(a)->http_status = 0;
  CUR(a)->pretty_valid = 0;
  CUR(a)->pretty_header_lines = 0;
  CUR(a)->exit_code = 0;
  CUR(a)->out_scroll = 0;
  CUR(a)->out_follow = 1;
}

static void on_out(void *user, const char *data, size_t len)
{
  tab_t *t = user;
  outbuf_append(t->out, data, len);
}

static void on_err(void *user, const char *data, size_t len)
{
  tab_t *t = user;
  outbuf_append(t->log, data, len);
}

/* Next to the settings, or in the system's temporary folder for a scripted run. */
static void run_file_stem(app_t *a, char *stem, size_t stem_n)
{
  static unsigned counter;
  char dir[SETTINGS_PATH_CAP];
  const char *slash;
  size_t n = 0;
  unsigned pid;
#ifdef _WIN32
  pid = (unsigned)GetCurrentProcessId();
#else
  pid = (unsigned)getpid();
#endif
  slash = strrchr(a->settings_path, '/');
#ifdef _WIN32
  if(!slash)
    slash = strrchr(a->settings_path, '\\');
#endif
  if(a->settings_path[0] && slash) {
    n = (size_t)(slash - a->settings_path);
    snprintf(dir, sizeof(dir), "%.*s", (int)n, a->settings_path);
  }
  else {
    const char *t = getenv("TMPDIR");
    if(!t)
      t = getenv("TEMP");
    snprintf(dir, sizeof(dir), "%s", t ? t : "/tmp");
  }
  snprintf(stem, stem_n, "%s/run-%u-%u", dir, pid, ++counter);
}

void app_run(app_t *a)
{
  /* Stats and the transfer log are captured with flags the user did not ask
     for. They never touch stdout, and the Log says they were added. */
  static const char write_out[] =
    "%{stderr}" MARKER_PREFIX "http=%{http_code} time=%{time_total} size=%{size_download}\n";
  const char *extra[8];
  int n = 0, have_v, i, capture = 1;
  const char **argv;
  char err[512], note[256];
  size_t note_n = 0;

  if(CUR(a)->proc)
    return;
  app_clear_output(a);
  app_rebuild_args(a);
  /* curl would only say "no URL specified"; say where it goes instead */
  CUR(a)->url_missing = request_lacks_url(&CUR(a)->req);
  if(CUR(a)->url_missing) {
    CUR(a)->out_tab = TAB_RESPONSE;
    return;
  }
  /* nothing leaves until every variable has a value */
  {
    str_list missing;
    if(vars_missing(&a->vars.e[a->vars.active], &CUR(a)->args_tmpl, &missing))
      return;
    if(missing.n) {
      modal_open_missing(a, &missing);
      return;
    }
    str_list_free(&missing);
  }
  mask_free(CUR(a)->mask);
  CUR(a)->mask = mask_build(&a->vars.e[a->vars.active], &CUR(a)->args_real);

  have_v = request_find(&CUR(a)->req, options_find("verbose")) >= 0 ||
           request_find(&CUR(a)->req, options_find("trace")) >= 0 ||
           request_find(&CUR(a)->req, options_find("trace-ascii")) >= 0;
  note[0] = '\0';
  if(!a->set.capture) {
    have_v = 1;
    capture = 0;
  }
  if(!have_v) {
    extra[n++] = "--verbose";
    note_n += (size_t)snprintf(note + note_n, sizeof(note) - note_n, " --verbose");
  }
  if(capture && curlinfo_supports(&a->curl, "no-progress-meter") && curlinfo_at_least(&a->curl, 7, 67)) {
    extra[n++] = "--no-progress-meter";
    note_n += (size_t)snprintf(note + note_n, sizeof(note) - note_n, " --no-progress-meter");
  }
  if(capture && request_find(&CUR(a)->req, options_find("write-out")) < 0 && curlinfo_at_least(&a->curl, 7, 63)) {
    extra[n++] = "--write-out";
    extra[n++] = write_out;
    note_n += (size_t)snprintf(note + note_n, sizeof(note) - note_n, " --write-out <stats>");
  }
  extra[n] = NULL;

  argv = arg_list_argv(&CUR(a)->args_real, extra);
  if(!argv)
    return;
  if(args_need_file(argv)) {
    /* too long for a command line: curl reads them from private files instead */
    const char *short_argv[16];
    int m = 0, k;
    size_t line_max = curlinfo_at_least(&a->curl, 8, 2) ? ARG_CONFIG_LINE_MAX : ARG_CONFIG_LINE_OLD;
    char stem[SETTINGS_PATH_CAP + 64], conf[SETTINGS_PATH_CAP + 80];
    free((void *)argv);
    argv = NULL;
    run_file_stem(a, stem, sizeof(stem));
    if(!args_spill_bodies(&CUR(a)->args_real, stem, &CUR(a)->run_files, err, sizeof(err))) {
      argv = arg_list_argv(&CUR(a)->args_real, extra);
      if(argv && args_need_file(argv)) {
        free((void *)argv);
        argv = NULL;
        snprintf(conf, sizeof(conf), "%s.conf", stem);
        if(!args_write_config(&CUR(a)->args_real, conf, line_max, err, sizeof(err))) {
          char **grown = realloc(CUR(a)->run_files.paths, sizeof(char *) * (size_t)(CUR(a)->run_files.n + 1));
          size_t cn = strlen(conf) + 1;
          char *copy = malloc(cn);
          if(grown)
            CUR(a)->run_files.paths = grown;
          if(grown && copy) {
            memcpy(copy, conf, cn);
            CUR(a)->run_files.paths[CUR(a)->run_files.n++] = copy;
            short_argv[m++] = CUR(a)->args_real.tok[0].text;
            if(CUR(a)->args_real.count > 1 && CUR(a)->args_real.tok[1].kind == TOK_FLAG &&
               CUR(a)->args_real.tok[1].opt == options_find("disable"))
              short_argv[m++] = CUR(a)->args_real.tok[1].text;
            short_argv[m++] = "--config";
            short_argv[m++] = copy;
            for(k = 0; k < n; k++)
              short_argv[m++] = extra[k];
            short_argv[m] = NULL;
            argv = malloc(sizeof(short_argv));
            if(argv)
              memcpy((void *)argv, short_argv, sizeof(short_argv));
          }
          else {
            free(copy);
            snprintf(err, sizeof(err), "out of memory");
          }
        }
      }
    }
    if(!argv) {
      char line[900];
      int len = snprintf(line, sizeof(line), "curl UI: %s. Use an option's @file form where it has one, "
                         "so that curl reads the file itself.\n", err);
      outbuf_append(CUR(a)->log, line, (size_t)len);
      args_files_remove(&CUR(a)->run_files);
      CUR(a)->out_tab = TAB_LOG;
      CUR(a)->has_result = 1;
      CUR(a)->exit_code = 2;
      return;
    }
  }
  if(note_n) {
    char line[320];
    int len = snprintf(line, sizeof(line), "* curl UI also passed%s, to fill this tab and the status chips\n", note);
    outbuf_append(CUR(a)->log, line, (size_t)len);
  }
  CUR(a)->proc = proc_start(argv, on_out, on_err, CUR(a), err, sizeof(err));
  free((void *)argv);
  CUR(a)->run_started = SDL_GetTicks();
  CUR(a)->run_secs = 0;
  if(!CUR(a)->proc) {
    char line[640];
    int len = snprintf(line, sizeof(line), "curl: %s\n", err);
    outbuf_append(CUR(a)->log, line, (size_t)len);
    CUR(a)->out_tab = TAB_LOG;
    CUR(a)->has_result = 1;
    CUR(a)->exit_code = 127;
  }
  for(i = 0; i < n; i++)
    (void)extra[i];
}

void app_stop(app_t *a)
{
  if(CUR(a)->proc)
    proc_kill(CUR(a)->proc);
}

/* Offers the body (minus any leading HTTP headers from --include) to the JSON
   printer, and keeps the headers above the result. */
static void make_pretty(tab_t *t)
{
  size_t len = 0, skip = 0, header_lines = 0, out_len = 0;
  char *text, *pretty;
  if(outbuf_truncated(t->out))
    return;
  text = outbuf_join(t->out, &len);
  if(!text)
    return;
  while(!strncmp(text + skip, "HTTP/", 5)) {
    const char *blank = strstr(text + skip, "\n\n");
    const char *p;
    if(!blank)
      break;
    for(p = text + skip; p <= blank; p++)
      if(*p == '\n')
        header_lines++;
    header_lines++;
    skip = (size_t)(blank - text) + 2;
  }
  pretty = json_pretty(text + skip, len - skip, &out_len);
  if(pretty) {
    outbuf_append(t->pretty_buf, text, skip);
    outbuf_append(t->pretty_buf, pretty, out_len);
    t->pretty_valid = 1;
    t->pretty_header_lines = header_lines;
  }
  free(pretty);
  free(text);
}

static void poll_tab(tab_t *t)
{
  char marker[OUTBUF_MARKER_CAP];
  if(!t->proc)
    return;
  t->run_secs = (float)(SDL_GetTicks() - t->run_started) / 1000.0f;
  if(!proc_finished(t->proc))
    return;
  t->exit_code = proc_wait(t->proc);
  proc_free(t->proc);
  t->proc = NULL;
  args_files_remove(&t->run_files);
  t->has_result = 1;
  if(outbuf_get_marker(t->log, marker, sizeof(marker))) {
    if(sscanf(marker, "http=%d time=%lf size=%lld", &t->http_status, &t->curl_time, &t->curl_size) == 3)
      t->has_stats = 1;
  }
  make_pretty(t);
  /* nothing on stdout and a failure: the explanation is in the log */
  if(t->exit_code && outbuf_line_count(t->out) == 0)
    t->out_tab = TAB_LOG;
}

void app_poll(app_t *a)
{
  int i;
  for(i = 0; i < a->ntabs; i++)
    poll_tab(a->tabs[i]);
}

void app_push(app_t *a, act_kind kind, int x, int y)
{
  if(a->nactions < MAX_ACTIONS) {
    a->actions[a->nactions].kind = kind;
    a->actions[a->nactions].a = x;
    a->actions[a->nactions].b = y;
    a->nactions++;
  }
}

/* Views only queue changes: nothing moves under a frame still drawing it. */
void app_apply_actions(app_t *a)
{
  int i;
  for(i = 0; i < a->nactions; i++) {
    action *act = &a->actions[i];
    int slot;
    switch(act->kind) {
    case ACT_TOGGLE:
      a->pending_unfocus = 1;
      app_toggle_option(a, act->a);
      break;
    case ACT_REMOVE:
      a->pending_unfocus = 1;
      request_remove(&CUR(a)->req, act->a);
      break;
    case ACT_ADD_VALUE:
      slot = request_find(&CUR(a)->req, act->a);
      if(slot >= 0)
        request_add_value(&CUR(a)->req.sel[slot]);
      break;
    case ACT_REMOVE_VALUE:
      a->pending_unfocus = 1;
      slot = request_find(&CUR(a)->req, act->a);
      if(slot >= 0) {
        request_remove_value(&CUR(a)->req.sel[slot], act->b);
        if(!CUR(a)->req.sel[slot].nvalues)
          request_remove(&CUR(a)->req, act->a);
      }
      break;
    case ACT_SET_NEG:
      slot = request_find(&CUR(a)->req, act->a);
      if(slot >= 0)
        CUR(a)->req.sel[slot].negated = act->b;
      break;
    case ACT_CLEAR:
      a->pending_unfocus = 1;
      request_clear_options(&CUR(a)->req);
      CUR(a)->show_recipes = 1;
      break;
    case ACT_RECIPE:
      a->pending_unfocus = 1;
      app_apply_recipe(a, act->a);
      break;
    case ACT_RUN:
      app_run(a);
      break;
    case ACT_USE_AS_URL:
      a->pending_unfocus = 1;
      slot = request_find(&CUR(a)->req, act->a);
      if(slot >= 0 && act->b < CUR(a)->req.sel[slot].nvalues) {
        char *v = CUR(a)->req.sel[slot].values[act->b];
        request_set_url(&CUR(a)->req, v);
        v[0] = '\0';
      }
      CUR(a)->url_missing = 0;
      break;
    case ACT_STOP:
      app_stop(a);
      break;
    case ACT_NONE:
      break;
    }
  }
  a->nactions = 0;
}

void app_set_doc(app_t *a, int opt, int push_history)
{
  if(opt == a->doc_opt)
    return;
  if(push_history && a->doc_opt >= 0 && a->doc_hist_n < DOC_HISTORY)
    a->doc_hist[a->doc_hist_n++] = a->doc_opt;
  if(!push_history)
    a->doc_hist_n = 0;
  a->doc_opt = opt;
  a->doc_scroll = 0;
}

void app_doc_back(app_t *a)
{
  if(a->doc_hist_n > 0) {
    a->doc_opt = a->doc_hist[--a->doc_hist_n];
    a->doc_scroll = 0;
  }
}
