#include "app.h"
#include "json_fmt.h"

#include <SDL.h>
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
const char *app_method_label(const app_t *a, int index)
{
  static const char *const body_opts[] = {
    "data", "data-ascii", "data-binary", "data-raw", "data-urlencode", "json", "form",
    "form-string", NULL
  };
  int i;
  if(index > 0)
    return method_names[index];
  for(i = 0; body_opts[i]; i++)
    if(request_find(&a->req, options_find(body_opts[i])) >= 0)
      return "POST";
  if(request_find(&a->req, options_find("upload-file")) >= 0)
    return "PUT";
  if(request_find(&a->req, options_find("head")) >= 0)
    return "HEAD";
  return "GET";
}

void app_init(app_t *a)
{
  memset(a, 0, sizeof(*a));
  request_init(&a->req);
  settings_defaults(&a->set);
  a->results = malloc(sizeof(int) * (size_t)g_option_count);
  a->out = outbuf_new(0);
  a->log = outbuf_new(0);
  a->pretty_buf = outbuf_new(0);
  outbuf_set_marker(a->log, MARKER_PREFIX);
  a->doc_opt = -1;
  a->doc_built_opt = -2;
  a->hover_cand = -1;
  a->out_follow = 1;
  a->filter_last = -1;
  a->doc_arena = malloc(8192);
}

void app_free(app_t *a)
{
  proc_free(a->proc);
  a->proc = NULL;
  arg_list_free(&a->args);
  request_free(&a->req);
  curlinfo_free(&a->curl);
  outbuf_free(a->out);
  outbuf_free(a->log);
  outbuf_free(a->pretty_buf);
  wrap_free(&a->wrap_out);
  wrap_free(&a->wrap_log);
  wrap_free(&a->wrap_pretty);
  docs_free(&a->doc);
  free(a->dops);
  free(a->doc_arena);
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
  arg_list_free(&a->args);
  a->req.no_curlrc = a->set.no_curlrc;
  a->req.config_file = a->set.config_file;
  snprintf(a->req.method, sizeof(a->req.method), "%s", method_names[a->method]);
  builder_build(&a->req, a->set.program[0] ? a->set.program : "curl", &a->args);
}

void app_add_option(app_t *a, const char *name, const char *value)
{
  int opt = options_find(name);
  sel_option *s;
  if(opt < 0)
    return;
  s = request_add(&a->req, opt);
  if(s && value && g_options[opt].arg[0]) {
    char *slot = NULL;
    int i;
    for(i = 0; i < s->nvalues && !slot; i++)
      if(!s->values[i][0])
        slot = s->values[i];
    if(!slot)
      slot = request_add_value(s);
    if(slot)
      snprintf(slot, BUILDER_VALUE_CAP, "%s", value);
  }
}

void app_toggle_option(app_t *a, int opt)
{
  if(request_find(&a->req, opt) >= 0)
    request_remove(&a->req, opt);
  else {
    request_add(&a->req, opt);
    a->show_recipes = 0;
  }
}

void app_apply_recipe(app_t *a, int index)
{
  const recipe *r = &g_recipes[index];
  int i;
  request_clear_options(&a->req);
  a->method = 0;
  snprintf(a->req.url, sizeof(a->req.url), "%s", r->url);
  for(i = 0; i < 5 && r->opts[i].name; i++)
    app_add_option(a, r->opts[i].name, r->opts[i].value);
  a->show_recipes = 0;
}

void app_clear_output(app_t *a)
{
  outbuf_clear(a->out);
  outbuf_clear(a->log);
  outbuf_clear(a->pretty_buf);
  a->has_result = 0;
  a->has_stats = 0;
  a->http_status = 0;
  a->pretty_valid = 0;
  a->pretty_header_lines = 0;
  a->exit_code = 0;
  a->out_scroll = 0;
  a->out_follow = 1;
}

static void on_out(void *user, const char *data, size_t len)
{
  app_t *a = user;
  outbuf_append(a->out, data, len);
}

static void on_err(void *user, const char *data, size_t len)
{
  app_t *a = user;
  outbuf_append(a->log, data, len);
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

  if(a->proc)
    return;
  app_clear_output(a);
  app_rebuild_args(a);

  have_v = request_find(&a->req, options_find("verbose")) >= 0 ||
           request_find(&a->req, options_find("trace")) >= 0 ||
           request_find(&a->req, options_find("trace-ascii")) >= 0;
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
  if(capture && request_find(&a->req, options_find("write-out")) < 0 && curlinfo_at_least(&a->curl, 7, 63)) {
    extra[n++] = "--write-out";
    extra[n++] = write_out;
    note_n += (size_t)snprintf(note + note_n, sizeof(note) - note_n, " --write-out <stats>");
  }
  extra[n] = NULL;

  argv = arg_list_argv(&a->args, extra);
  if(!argv)
    return;
  if(note_n) {
    char line[320];
    int len = snprintf(line, sizeof(line), "* curl UI also passed%s, to fill this tab and the status chips\n", note);
    outbuf_append(a->log, line, (size_t)len);
  }
  a->proc = proc_start(argv, on_out, on_err, a, err, sizeof(err));
  free((void *)argv);
  a->run_started = SDL_GetTicks();
  a->run_secs = 0;
  if(!a->proc) {
    char line[640];
    int len = snprintf(line, sizeof(line), "curl: %s\n", err);
    outbuf_append(a->log, line, (size_t)len);
    a->tab = TAB_LOG;
    a->has_result = 1;
    a->exit_code = 127;
  }
  for(i = 0; i < n; i++)
    (void)extra[i];
}

void app_stop(app_t *a)
{
  if(a->proc)
    proc_kill(a->proc);
}

/* Offers the body (minus any leading HTTP headers from --include) to the JSON
   printer, and keeps the headers above the result. */
static void make_pretty(app_t *a)
{
  size_t len = 0, skip = 0, header_lines = 0, out_len = 0;
  char *text, *pretty;
  if(outbuf_truncated(a->out))
    return;
  text = outbuf_join(a->out, &len);
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
    outbuf_append(a->pretty_buf, text, skip);
    outbuf_append(a->pretty_buf, pretty, out_len);
    a->pretty_valid = 1;
    a->pretty_header_lines = header_lines;
  }
  free(pretty);
  free(text);
}

void app_poll(app_t *a)
{
  char marker[OUTBUF_MARKER_CAP];
  if(!a->proc)
    return;
  a->run_secs = (float)(SDL_GetTicks() - a->run_started) / 1000.0f;
  if(!proc_finished(a->proc))
    return;
  a->exit_code = proc_wait(a->proc);
  proc_free(a->proc);
  a->proc = NULL;
  a->has_result = 1;
  if(outbuf_get_marker(a->log, marker, sizeof(marker))) {
    if(sscanf(marker, "http=%d time=%lf size=%lld", &a->http_status, &a->curl_time, &a->curl_size) == 3)
      a->has_stats = 1;
  }
  make_pretty(a);
  /* nothing on stdout and a failure: the explanation is in the log */
  if(a->exit_code && outbuf_line_count(a->out) == 0)
    a->tab = TAB_LOG;
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
      request_remove(&a->req, act->a);
      break;
    case ACT_ADD_VALUE:
      slot = request_find(&a->req, act->a);
      if(slot >= 0)
        request_add_value(&a->req.sel[slot]);
      break;
    case ACT_REMOVE_VALUE:
      a->pending_unfocus = 1;
      slot = request_find(&a->req, act->a);
      if(slot >= 0) {
        request_remove_value(&a->req.sel[slot], act->b);
        if(!a->req.sel[slot].nvalues)
          request_remove(&a->req, act->a);
      }
      break;
    case ACT_SET_NEG:
      slot = request_find(&a->req, act->a);
      if(slot >= 0)
        a->req.sel[slot].negated = act->b;
      break;
    case ACT_CLEAR:
      a->pending_unfocus = 1;
      request_clear_options(&a->req);
      a->show_recipes = 1;
      break;
    case ACT_RECIPE:
      a->pending_unfocus = 1;
      app_apply_recipe(a, act->a);
      break;
    case ACT_RUN:
      app_run(a);
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
