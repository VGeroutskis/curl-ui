/* curl UI: the SDL window, event loop, and the scripted test hooks. */
#ifdef __linux__
#define _DEFAULT_SOURCE  /* unsetenv */
#endif
#define NK_IMPLEMENTATION
#define NK_SDL_RENDERER_IMPLEMENTATION
#include <SDL.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nk.h"
#include "nuklear_sdl_renderer.h"

#include "app.h"
#include "argfile.h"
#include "fileio.h"
#include "fonts_data.h"

#ifdef _WIN32
#include <direct.h>
#define chdir _chdir
#define getcwd _getcwd
#else
#include <unistd.h>
#endif

#define MIN_W 1180
#define MIN_H 640
#define MAX_SCRIPT 64

static app_t g_app;
static float g_scale = 1.0f;   /* device pixels per UI unit: fonts are baked for it */
static float g_zoom = 1.0f;    /* UI units per window unit */
static float g_auto_zoom = 1.0f;
static SDL_Cursor *g_cursors[CURSOR_COUNT];
static SDL_Renderer *g_renderer;
static SDL_Window *g_window;
static double g_frame_ms_total, g_frame_ms_max;
static long g_frames;
static int g_trace;  /* CURLUI_TRACE=1 logs input and sizes, for bug reports */

/* Bake options that keep text sharp: snapped to whole pixels, no oversampling,
   and the logical height is the baked one divided by the DPI scale. */
static struct nk_font *bake(struct nk_font_atlas *atlas, const unsigned char *data, unsigned len,
                            const unsigned *ranges, float height)
{
  struct nk_font_config cfg = nk_font_config(height * g_scale);
  struct nk_font *font;
  cfg.pixel_snap = 1;
  cfg.oversample_h = 1;
  cfg.oversample_v = 1;
  cfg.range = (const nk_rune *)ranges;
  cfg.ttf_data_owned_by_atlas = 0;  /* the atlas frees its copy when rebaked */
  font = nk_font_atlas_add_from_memory(atlas, (void *)data, len, height * g_scale, &cfg);
  return font;
}

static void load_fonts(app_t *a)
{
  struct nk_font_atlas *atlas;
  struct nk_font *fnt[7];
  int i;
  if(sdl.ogl.font_tex) {  /* a rebake: the text size or the display changed */
    nk_font_atlas_clear(&sdl.atlas);
    SDL_DestroyTexture(sdl.ogl.font_tex);
    sdl.ogl.font_tex = NULL;
    a->doc_built_opt = -2;
  }
  nk_sdl_font_stash_begin(&atlas);
  fnt[0] = bake(atlas, plex_sans_regular_ttf, plex_sans_regular_ttf_len, plex_sans_regular_ranges, 14);
  fnt[1] = bake(atlas, plex_sans_semibold_ttf, plex_sans_semibold_ttf_len, plex_sans_semibold_ranges, 14);
  fnt[2] = bake(atlas, plex_sans_regular_ttf, plex_sans_regular_ttf_len, plex_sans_regular_ranges, 12);
  fnt[3] = bake(atlas, plex_sans_semibold_ttf, plex_sans_semibold_ttf_len, plex_sans_semibold_ranges, 17);
  fnt[4] = bake(atlas, plex_mono_regular_ttf, plex_mono_regular_ttf_len, plex_mono_regular_ranges, 13);
  fnt[5] = bake(atlas, plex_mono_semibold_ttf, plex_mono_semibold_ttf_len, plex_mono_semibold_ranges, 13);
  fnt[6] = bake(atlas, plex_mono_regular_ttf, plex_mono_regular_ttf_len, plex_mono_regular_ranges, 12);
  nk_sdl_font_stash_end();
  /* Glyphs sit at whole UI units. At a whole scale those are whole pixels, and
     nearest keeps text sharp; at 125% they are not, and nearest would drop
     columns of the glyphs. */
  {
    float frac = g_scale - (float)(int)(g_scale + 0.5f);
    SDL_SetTextureScaleMode(sdl.ogl.font_tex,
                            (frac > 0.01f || frac < -0.01f) ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
  }
  for(i = 0; i < 7; i++)
    fnt[i]->handle.height /= g_scale;
  a->f.sans = &fnt[0]->handle;
  a->f.semi = &fnt[1]->handle;
  a->f.small = &fnt[2]->handle;
  a->f.title = &fnt[3]->handle;
  a->f.mono = &fnt[4]->handle;
  a->f.mono_semi = &fnt[5]->handle;
  a->f.mono_small = &fnt[6]->handle;
  a->f.mono_w = ui_text_w(a->f.mono, "0", 1);
  a->f.mono_small_w = ui_text_w(a->f.mono_small, "0", 1);
  nk_style_set_font(a->ctx, a->f.sans);
}

static void apply_cursor(cursor_kind kind)
{
  static cursor_kind current = CURSOR_ARROW;
  if(kind != current && g_cursors[kind]) {
    SDL_SetCursor(g_cursors[kind]);
    current = kind;
  }
}

/* On Wayland, sdl2-compat can report the window in device pixels, so the
   output/window ratio says 1 at any desktop scale: there the DPI is the only
   hint. X11 DPI often reflects the panel rather than the desktop scale, so it
   is not trusted; Retina and friends show up in the ratio. */
static float detect_zoom(int w, int ow)
{
  const char *driver = SDL_GetCurrentVideoDriver();
  float ddpi = 0, z;
  if(!driver || (strcmp(driver, "wayland") && strcmp(driver, "windows")))
    return 1.0f;
  if((float)ow / (float)w > 1.01f)
    return 1.0f;
  if(SDL_GetDisplayDPI(SDL_GetWindowDisplayIndex(g_window), &ddpi, NULL, NULL) || ddpi <= 0)
    return 1.0f;
  z = (float)((int)(ddpi / 96.0f * 4.0f + 0.5f)) / 4.0f;
  return z < 1.0f ? 1.0f : (z > 3.0f ? 3.0f : z);
}

static void apply_scale(app_t *a, int w, int ow)
{
  float zoom, scale;
  g_auto_zoom = detect_zoom(w, ow);
  zoom = a->set.zoom ? (float)a->set.zoom / 100.0f : g_auto_zoom;
  scale = (float)ow / (float)w * zoom;
  if(scale != g_scale) {
    g_scale = scale;
    load_fonts(a);
  }
  if(zoom != g_zoom) {
    g_zoom = zoom;
    /* the smallest window still holds the whole layout, if the display can */
    {
      SDL_Rect bounds;
      int mw = (int)((float)MIN_W * zoom), mh = (int)((float)MIN_H * zoom);
      if(!SDL_GetDisplayUsableBounds(SDL_GetWindowDisplayIndex(g_window), &bounds)) {
        mw = NK_MIN(mw, bounds.w);
        mh = NK_MIN(mh, bounds.h);
      }
      SDL_SetWindowMinimumSize(g_window, mw, mh);
    }
  }
}

static SDL_HitTestResult SDLCALL hit_test(SDL_Window *win, const SDL_Point *pt, void *data)
{
  app_t *a = data;
  (void)win;
  switch(win_hit_test((float)pt->x / g_zoom, (float)pt->y / g_zoom, a->W, a->H, 48, a->drag_right, a->win_maximized)) {
  case WIN_HIT_DRAG: return SDL_HITTEST_DRAGGABLE;
  case WIN_HIT_N: return SDL_HITTEST_RESIZE_TOP;
  case WIN_HIT_S: return SDL_HITTEST_RESIZE_BOTTOM;
  case WIN_HIT_E: return SDL_HITTEST_RESIZE_RIGHT;
  case WIN_HIT_W: return SDL_HITTEST_RESIZE_LEFT;
  case WIN_HIT_NE: return SDL_HITTEST_RESIZE_TOPRIGHT;
  case WIN_HIT_NW: return SDL_HITTEST_RESIZE_TOPLEFT;
  case WIN_HIT_SE: return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
  case WIN_HIT_SW: return SDL_HITTEST_RESIZE_BOTTOMLEFT;
  default: return SDL_HITTEST_NORMAL;
  }
}

/* The cursor says an edge can be pulled. */
static void edge_cursor(app_t *a)
{
  const struct nk_input *in = &a->ctx->input;
  if(!a->own_frame || a->win_maximized)
    return;
  switch(win_hit_test(in->mouse.pos.x, in->mouse.pos.y, a->W, a->H, 0, a->W, 0)) {
  case WIN_HIT_N: case WIN_HIT_S: a->cursor = CURSOR_NS; break;
  case WIN_HIT_E: case WIN_HIT_W: a->cursor = CURSOR_EW; break;
  case WIN_HIT_NW: case WIN_HIT_SE: a->cursor = CURSOR_NWSE; break;
  case WIN_HIT_NE: case WIN_HIT_SW: a->cursor = CURSOR_NESW; break;
  default: break;
  }
}

static void window_action(app_t *a)
{
  win_do act = a->win_action;
  a->win_action = WIN_DO_NONE;
  if(act == WIN_DO_NONE)
    return;
  a->win_last = act;
  if(act == WIN_DO_MIN)
    SDL_MinimizeWindow(g_window);
  else if(act == WIN_DO_MAX) {
    if(SDL_GetWindowFlags(g_window) & SDL_WINDOW_MAXIMIZED)
      SDL_RestoreWindow(g_window);
    else
      SDL_MaximizeWindow(g_window);
  }
  else {
    /* the same way as any other request to close: unsaved work is asked about */
    SDL_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = SDL_QUIT;
    SDL_PushEvent(&ev);
  }
}

static void frame(app_t *a)
{
  int w, h, ow, oh;
  Uint64 t0, t1;
  SDL_GetWindowSize(g_window, &w, &h);
  SDL_GetRendererOutputSize(g_renderer, &ow, &oh);
  apply_scale(a, w, ow);
  a->W = (float)w / g_zoom;
  a->H = (float)h / g_zoom;
  a->now_ms = SDL_GetTicks();
  a->blink_off = (a->now_ms / 500u) & 1u;
  a->win_maximized = (SDL_GetWindowFlags(g_window) & SDL_WINDOW_MAXIMIZED) != 0;
  app_poll(a);

  t0 = SDL_GetPerformanceCounter();
  view_frame(a);
  t1 = SDL_GetPerformanceCounter();
  {
    double ms = (double)(t1 - t0) * 1000.0 / (double)SDL_GetPerformanceFrequency();
    g_frame_ms_total += ms;
    if(ms > g_frame_ms_max)
      g_frame_ms_max = ms;
    g_frames++;
  }
  app_apply_actions(a);
  window_action(a);
  edge_cursor(a);
  apply_cursor(a->cursor);

  SDL_SetRenderDrawColor(g_renderer, a->pal.bg.r, a->pal.bg.g, a->pal.bg.b, 255);
  SDL_RenderClear(g_renderer);
  SDL_RenderSetScale(g_renderer, a->W > 0 ? (float)ow / a->W : 1, a->H > 0 ? (float)oh / a->H : 1);
  nk_sdl_render(NK_ANTI_ALIASING_ON);
  SDL_RenderSetScale(g_renderer, 1, 1);
}

/* ---- scripted test hooks ---- */

typedef enum { SC_MOUSE, SC_CLICK, SC_PASTE, SC_SAVE_AS, SC_MODAL_VALUE, SC_MODAL_ACTION, SC_SELECT_TAB, SC_CLOSE_TAB, SC_TYPE, SC_WHEEL, SC_DOUBLE } script_kind;
typedef struct {
  script_kind kind;
  int x, y, dy;
  const char *text;
} script_op;

static void input_frame(app_t *a, void (*feed)(struct nk_context *, const void *), const void *arg)
{
  nk_input_begin(a->ctx);
  if(feed)
    feed(a->ctx, arg);
  nk_input_end(a->ctx);
  frame(a);
}

static void feed_motion(struct nk_context *c, const void *arg)
{
  const script_op *op = arg;
  nk_input_motion(c, op->x, op->y);
}

static void feed_down(struct nk_context *c, const void *arg)
{
  const script_op *op = arg;
  nk_input_button(c, NK_BUTTON_LEFT, op->x, op->y, nk_true);
}

static void feed_up(struct nk_context *c, const void *arg)
{
  const script_op *op = arg;
  nk_input_button(c, NK_BUTTON_LEFT, op->x, op->y, nk_false);
}

static void feed_text(struct nk_context *c, const void *arg)
{
  const char *s = arg;
  nk_rune rune;
  int n = nk_utf_decode(s, &rune, 4);
  if(n)
    nk_input_unicode(c, rune);
}

static void feed_wheel(struct nk_context *c, const void *arg)
{
  const script_op *op = arg;
  nk_input_scroll(c, nk_vec2(0, (float)op->dy));
}

static void feed_paste(struct nk_context *c, const void *arg)
{
  (void)arg;
  nk_input_key(c, NK_KEY_PASTE, nk_true);
}

static void feed_paste_up(struct nk_context *c, const void *arg)
{
  (void)arg;
  nk_input_key(c, NK_KEY_PASTE, nk_false);
}

static void feed_enter(struct nk_context *c, const void *arg)
{
  (void)arg;
  nk_input_key(c, NK_KEY_ENTER, nk_true);
}

static void feed_enter_up(struct nk_context *c, const void *arg)
{
  (void)arg;
  nk_input_key(c, NK_KEY_ENTER, nk_false);
}

/* Frames until the UI has settled, including the hover dwell. */
static void settle(app_t *a)
{
  int i;
  for(i = 0; i < 3; i++)
    input_frame(a, NULL, NULL);
  SDL_Delay(130);
  for(i = 0; i < 3; i++)
    input_frame(a, NULL, NULL);
}

static void wait_run(app_t *a)
{
  Uint32 t0 = SDL_GetTicks();
  while(app_any_running(a) && SDL_GetTicks() - t0 < 20000) {
    input_frame(a, NULL, NULL);
    SDL_Delay(10);
  }
  settle(a);
}

static void run_script(app_t *a, const script_op *ops, int n)
{
  int i;
  for(i = 0; i < n; i++) {
    const script_op *op = &ops[i];
    switch(op->kind) {
    case SC_MOUSE:
      input_frame(a, feed_motion, op);
      settle(a);
      break;
    case SC_CLICK:
      input_frame(a, feed_motion, op);
      input_frame(a, feed_down, op);
      input_frame(a, feed_up, op);
      settle(a);
      break;
    case SC_DOUBLE:
      input_frame(a, feed_motion, op);
      input_frame(a, feed_down, op);
      input_frame(a, feed_up, op);
      input_frame(a, feed_down, op);
      input_frame(a, feed_up, op);
      settle(a);
      break;
    case SC_WHEEL:
      input_frame(a, feed_motion, op);
      input_frame(a, feed_wheel, op);
      settle(a);
      break;
    case SC_SAVE_AS:
      if(app_save_as_path(a, op->text))
        fprintf(stderr, "--save-as failed: %s\n", op->text);
      settle(a);
      break;
    case SC_MODAL_VALUE: {
      const char *eq = strchr(op->text, '=');
      int m;
      for(m = 0; eq && a->modal_open && m < a->modal_names.n; m++) {
        if(strlen(a->modal_names.s[m]) == (size_t)(eq - op->text) &&
           !memcmp(a->modal_names.s[m], op->text, (size_t)(eq - op->text))) {
          size_t need = strlen(eq + 1) + 1;
          if(need > a->modal_caps[m]) {
            a->modal_values[m] = realloc(a->modal_values[m], need);
            a->modal_caps[m] = need;
          }
          memcpy(a->modal_values[m], eq + 1, need);
        }
      }
      settle(a);
      break;
    }
    case SC_MODAL_ACTION:
      if(a->modal_open)
        modal_act(a, !strcmp(op->text, "save") ? MODAL_SAVE : !strcmp(op->text, "open") ? MODAL_OPEN_VARS : MODAL_CANCEL);
      settle(a);
      break;
    case SC_SELECT_TAB:
      CUR(a)->reveal = 0;
      if(op->x >= 0 && op->x < a->ntabs)
        a->cur = op->x;
      a->pending_unfocus = 1;
      settle(a);
      break;
    case SC_CLOSE_TAB:
      tab_close(a, op->x);
      settle(a);
      break;
    case SC_PASTE:
      if(op->text) {
        char *clip = file_read_all(op->text, NULL, (size_t)1 << 30);
        if(clip) {
          SDL_SetClipboardText(clip);
          free(clip);
        }
      }
      input_frame(a, feed_paste, NULL);
      input_frame(a, feed_paste_up, NULL);
      settle(a);
      break;
    case SC_TYPE: {
      const char *s = op->text;
      while(*s) {
        nk_rune rune;
        int len = nk_utf_decode(s, &rune, 4);
        if(!len)
          break;
        if(*s == '\n') {
          input_frame(a, feed_enter, NULL);
          input_frame(a, feed_enter_up, NULL);
        }
        else
          input_frame(a, feed_text, s);
        s += len;
      }
      settle(a);
      break;
    }
    }
  }
}

/* What the run produced, for tests that look at more than pixels. */
static int write_dump(app_t *a, const char *path)
{
  FILE *f = fopen(path, "wb");
  char *cmd = builder_preview(CUR(a)->reveal ? &CUR(a)->args_real : &CUR(a)->args);
  char *real = builder_preview(&CUR(a)->args_real);
  size_t out_len = 0, log_len = 0;
  char *out = outbuf_join(CUR(a)->out, &out_len);
  char *log = outbuf_join(CUR(a)->log, &log_len);
  mask_apply(CUR(a)->mask, out, out_len);
  mask_apply(CUR(a)->mask, log, log_len);
  int i;
  if(f) {
    fprintf(f, "exit=%d\nhad_result=%d\nhttp=%d\nstats=%d\nsize=%lld\nurl_missing=%d\n", CUR(a)->exit_code, CUR(a)->has_result,
            CUR(a)->http_status, CUR(a)->has_stats, CUR(a)->curl_size, CUR(a)->url_missing);
    fprintf(f, "listed=%d\nhidden=%d\ntabs=%d\ncur=%d\n", a->nresults, a->nhidden, a->ntabs, a->cur);
    form_params_sync(CUR(a));
    for(i = 0; i < CUR(a)->params.n; i++)
      fprintf(f, "param=%s:%s\n", CUR(a)->params.r[i].key, CUR(a)->params.r[i].value);
    {
      static const char *const body_names[] = { "none", "raw", "json", "form-data", "urlencoded", "file", "options" };
      fprintf(f, "body_kind=%s\n", body_names[form_body_kind(&CUR(a)->req)]);
    }
    {
      int k, nodes = 0;
      for(k = 0; k < a->ncolls; k++)
        nodes += a->colls[k]->root->nkids;
      fprintf(f, "coll_top_nodes=%d\nauth=%s:%s\n", nodes, CUR(a)->auth_a ? CUR(a)->auth_a : "", CUR(a)->auth_b ? CUR(a)->auth_b : "");
      for(k = 0; k < CUR(a)->body_rows.n; k++)
        fprintf(f, "body_row=%s:%s:%d\n", CUR(a)->body_rows.r[k].key, CUR(a)->body_rows.r[k].value, CUR(a)->body_rows.r[k].enabled);
    }
    fprintf(f, "node=%s\ndirty=%d\ntitle=%s\n", CUR(a)->node_id, CUR(a)->dirty, CUR(a)->title);
    fprintf(f, "env=%s\nvars_for=%s\nshow_vars=%d\nnotice=%s\nmodal=%d\nmodal_missing=", app_tab_env(a, CUR(a))->name, a->vars_for, a->show_vars,
            a->notice, a->modal_open);
    for(i = 0; i < a->modal_names.n; i++)
      fprintf(f, "%s%s", i ? "," : "", a->modal_names.s[i]);
    fputs("\nmodal_secret=", f);
    for(i = 0; i < a->modal_names.n; i++)
      fprintf(f, "%s%d", i ? "," : "", a->modal_secret[i]);
    fputc('\n', f);
    fprintf(f, "command=%s\n---stdout---\n%s\n---log---\n%s\n", cmd ? cmd : "", out ? out : "", log ? log : "");
    for(i = 0; i < a->ntabs; i++) {
      size_t text_len = 0;
      char *text = outbuf_join(a->tabs[i]->out, &text_len);
      mask_apply(a->tabs[i]->mask, text, text_len);
      fprintf(f, "===tab %d exit=%d http=%d\n%s\n", i, a->tabs[i]->exit_code, a->tabs[i]->http_status, text ? text : "");
      free(text);
    }
    fprintf(f, "copy_command=%s\n", real ? real : "");
    {
      static const char *const modes[] = { "none", "open", "save", "dir" };
      static const char *const acts[] = { "none", "min", "max", "close" };
      int curl = CUR(a)->form == FORM_CURL && !a->show_settings && !a->show_vars;
      fprintf(f, "picker=%d\npicker_mode=%s\npicker_dir=%s\npicker_err=%s\npicker_confirm=%d\nlast_dir=%s\n", a->picker.open,
              modes[a->picker.mode], a->picker.dir ? a->picker.dir : "", a->picker.err, a->picker.confirm, a->set.last_dir);
      fprintf(f, "own_frame=%d\nwin_action=%s\nright=%s\npanel_vars=", a->own_frame, acts[a->win_last],
              !curl ? "request" : a->set.docs_hidden ? "none" : "docs");
      if(!curl) {
        var_env *env = app_tab_env(a, CUR(a));
        context_panel_names(a, CUR(a));
        for(i = 0; i < a->panel_names.n; i++) {
          const var_t *v = var_find(env, a->panel_names.s[i]);
          fprintf(f, "%s%s:%s", i ? "," : "", a->panel_names.s[i], !v ? "missing" : v->secret ? "********" : v->value);
        }
      }
      fputc('\n', f);
    }
    fclose(f);
  }
  free(real);
  free(cmd);
  free(out);
  free(log);
  return f ? 0 : -1;
}

static int save_screenshot(const char *path)
{
  int w, h, rc;
  SDL_Surface *surf;
  SDL_GetRendererOutputSize(g_renderer, &w, &h);
  surf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
  if(!surf)
    return -1;
  rc = SDL_RenderReadPixels(g_renderer, NULL, SDL_PIXELFORMAT_ARGB8888, surf->pixels, surf->pitch);
  if(!rc)
    rc = SDL_SaveBMP(surf, path);
  SDL_FreeSurface(surf);
  return rc;
}

/* Launched from a menu the working directory is often / where -O cannot write. */
static void pick_workdir(void)
{
  char cwd[1024];
  const char *home;
#ifdef _WIN32
  home = getenv("USERPROFILE");
  if(getcwd(cwd, sizeof(cwd)) && strstr(cwd, "\\Windows\\") == NULL && strstr(cwd, "Program Files") == NULL)
    return;
#else
  home = getenv("HOME");
  if(getcwd(cwd, sizeof(cwd)) && strcmp(cwd, "/") != 0)
    return;
#endif
  if(home && *home && chdir(home)) {
    /* stay where we are */
  }
}

static const char usage[] =
  "curl-ui [options]\n"
  "  --url URL           request URL\n"
  "  --method NAME       GET, POST, PUT, ...\n"
  "  --add NAME[=VALUE]  add an option, repeatable\n"
  "  --neg NAME          add an option in its --no- form\n"
  "  --run               run the request at startup\n"
  "  --dump FILE         with --screenshot: save exit code, output and log\n"
  "  --light             light theme\n"
  "  --docs NAME         open the docs of an option\n"
  "  --search TEXT       type into the library search\n"
  "  --log               show the Log tab\n"
  "  --size WxH          window size\n"
  "  --mouse X,Y         scripted mouse move\n"
  "  --click X,Y         scripted click\n"
  "  --wheel X,Y,DELTA   scripted mouse wheel\n"
  "  --type TEXT         scripted typing into the focused field (newline is Enter)\n"
  "  --screenshot F.bmp  after the above, save a screenshot and exit\n"
  "  --settings-dir DIR  keep settings in DIR instead of the per-user location\n"
  "  README.md lists the options for tabs, variables, forms and collections\n"
  "  --stats             print UI time per frame on exit\n";

#define DECOR_RESTART_ENV "CURLUI_PRELOAD_WAS"

int main(int argc, char **argv)
{
  app_t *a = &g_app;
#ifdef __linux__
  const char *preload_was = getenv(DECOR_RESTART_ENV);
  int decor_restarted = preload_was != NULL;
#endif
  script_op ops[MAX_SCRIPT];
  int win_size_given = 0;
  int nops = 0, no_wait = 0, want_log = 0, light = 0, stats = 0, win_w = 1360, win_h = 840, i;
  const char *settings_dir = NULL, *dump = NULL;
  char request_path[SETTINGS_PATH_CAP] = "";
  int have_cli_request = 0;
  int no_system_bar = 0;
  settings_t saved;
  const char *screenshot = NULL, *docs = NULL, *search = NULL, *method = NULL;
  int ow, oh, ww, wh;
  int need_frames = 3;
  SDL_Event held;
  int have_held = 0;
  Uint32 render_flags = SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC;

#ifdef __linux__
  if(decor_restarted) {
    if(*preload_was)
      SDL_setenv("LD_PRELOAD", preload_was, 1);
    else
      unsetenv("LD_PRELOAD");
    unsetenv(DECOR_RESTART_ENV);
  }
#endif
  pick_workdir();
  app_init(a);
  g_trace = getenv("CURLUI_TRACE") != NULL;

  /* The settings come first, so that the command line overrides them. Scripted
     runs stay away from the user's real settings unless told otherwise. */
  for(i = 1; i < argc; i++) {
    if(!strcmp(argv[i], "--screenshot"))
      screenshot = "";
    else if(!strcmp(argv[i], "--settings-dir") && i + 1 < argc)
      settings_dir = argv[i + 1];
    else if(!strcmp(argv[i], "--url") || !strcmp(argv[i], "--add"))
      have_cli_request = 1;
  }
  if((settings_dir || !screenshot) && !settings_path(settings_dir, "settings.ini", a->settings_path, sizeof(a->settings_path))) {
    char dir[SETTINGS_PATH_CAP];
    const char *slash = strrchr(a->settings_path, '/');
    if(!slash)
      slash = strrchr(a->settings_path, '\\');
    if(slash) {
      snprintf(dir, sizeof(dir), "%.*s", (int)(slash - a->settings_path), a->settings_path);
      args_remove_stale(dir);
    }
    settings_load(&a->set, a->settings_path);
    if(!settings_path(settings_dir, "variables.json", a->vars_path, sizeof(a->vars_path))) {
      char err[4096];
      if(book_load(&a->book, a->vars_path, err, sizeof(err)))
        app_notice(a, err);
    }
    settings_path(settings_dir, "history.jsonl", a->hist_path, sizeof(a->hist_path));
    if(!settings_path(settings_dir, "collections/x", a->coll_dir, sizeof(a->coll_dir))) {
      char *slash = strrchr(a->coll_dir, '/');
      if(!slash)
        slash = strrchr(a->coll_dir, '\\');
      if(slash)
        *slash = '\0';
      app_store_load(a);
    }
    settings_path(settings_dir, "session.json", a->session_path, sizeof(a->session_path));
    /* a command line for a person opens next to the tabs; a script starts clean */
    if(a->set.reopen_tabs && (!have_cli_request || !screenshot)) {
      app_session_load(a);
      if(have_cli_request && (CUR(a)->req.url[0] || CUR(a)->req.nsel))
        tab_new(a);
    }
    /* what an older version remembered becomes a tab, once */
    settings_path(settings_dir, "last-request.txt", request_path, sizeof(request_path));
    if(a->set.legacy_remember) {
      if(tab_new(a) >= 0 && request_load(&CUR(a)->req, &CUR(a)->method, request_path) != 1 && a->ntabs > 1)
        tab_close(a, a->cur);
      remove(request_path);
      a->set.legacy_remember = 0;
    }
  }
  app_ensure_default_collection(a);
  app_tab_store(a, CUR(a));  /* an empty table is nothing to save */
  a->vars_hash = app_vars_hash(a);
  CUR(a)->pretty = a->set.pretty;
  screenshot = NULL;

#define NEXT() (++i < argc ? argv[i] : (fprintf(stderr, "%s needs a value\n", argv[i - 1]), exit(2), ""))
  for(i = 1; i < argc; i++) {
    const char *arg = argv[i];
    if(!strcmp(arg, "--help") || !strcmp(arg, "-h")) {
      fputs(usage, stdout);
      return 0;
    }
    else if(!strcmp(arg, "--add")) {
      char name[128];
      const char *v = NEXT();
      const char *eq = strchr(v, '=');
      snprintf(name, sizeof(name), "%.*s", eq ? (int)(eq - v) : (int)strlen(v), v);
      if(options_find(name) < 0) {
        fprintf(stderr, "unknown option: %s\n", name);
        return 2;
      }
      app_add_option(a, name, eq ? eq + 1 : NULL);
    }
    else if(!strcmp(arg, "--add-file")) {
      /* NAME=PATH: the value comes from a file, for sizes no argument can carry */
      char name[128];
      const char *v = NEXT();
      const char *eq = strchr(v, '=');
      char *text;
      if(!eq || options_find((snprintf(name, sizeof(name), "%.*s", (int)(eq - v), v), name)) < 0) {
        fprintf(stderr, "bad --add-file: %s\n", v);
        return 2;
      }
      text = file_read_all(eq + 1, NULL, (size_t)1 << 30);
      if(!text) {
        fprintf(stderr, "cannot read %s\n", eq + 1);
        return 2;
      }
      app_add_option(a, name, text);
      free(text);
    }
    else if(!strcmp(arg, "--neg")) {
      const char *name = NEXT();
      int opt = options_find(name);
      sel_option *sel;
      if(opt < 0 || !g_options[opt].neg[0]) {
        fprintf(stderr, "no --no- form: %s\n", name);
        return 2;
      }
      sel = request_add(&CUR(a)->req, opt);
      if(sel)
        sel->negated = 1;
    }
    else if(!strcmp(arg, "--dump"))
      dump = NEXT();
    else if(!strcmp(arg, "--url"))
      request_set_url(&CUR(a)->req, NEXT());
    else if(!strcmp(arg, "--method"))
      method = NEXT();
    else if(!strcmp(arg, "--run"))
      CUR(a)->run_at_start = 1;
    else if(!strcmp(arg, "--light"))
      light = 1;
    else if(!strcmp(arg, "--docs"))
      docs = NEXT();
    else if(!strcmp(arg, "--search"))
      search = NEXT();
    else if(!strcmp(arg, "--log"))
      want_log = 1;
    else if(!strcmp(arg, "--stats"))
      stats = 1;
    else if(!strcmp(arg, "--size")) {
      if(sscanf(NEXT(), "%dx%d", &win_w, &win_h) != 2) {
        fprintf(stderr, "--size wants WxH\n");
        return 2;
      }
      win_size_given = 1;
    }
    else if(!strcmp(arg, "--screenshot"))
      screenshot = NEXT();
    else if(!strcmp(arg, "--settings-dir"))
      (void)NEXT();
    else if((!strcmp(arg, "--mouse") || !strcmp(arg, "--click")) && nops < MAX_SCRIPT) {
      script_op *op = &ops[nops++];
      op->kind = arg[2] == 'm' ? SC_MOUSE : SC_CLICK;
      if(sscanf(NEXT(), "%d,%d", &op->x, &op->y) != 2) {
        fprintf(stderr, "%s wants X,Y\n", arg);
        return 2;
      }
    }
    else if(!strcmp(arg, "--double-click") && nops < MAX_SCRIPT) {
      script_op *op = &ops[nops++];
      op->kind = SC_DOUBLE;
      if(sscanf(NEXT(), "%d,%d", &op->x, &op->y) != 2) {
        fprintf(stderr, "--double-click wants X,Y\n");
        return 2;
      }
    }
    else if(!strcmp(arg, "--wheel") && nops < MAX_SCRIPT) {
      script_op *op = &ops[nops++];
      op->kind = SC_WHEEL;
      if(sscanf(NEXT(), "%d,%d,%d", &op->x, &op->y, &op->dy) != 3) {
        fprintf(stderr, "--wheel wants X,Y,DELTA\n");
        return 2;
      }
    }
    else if((!strcmp(arg, "--select-tab") || !strcmp(arg, "--close-tab")) && nops < MAX_SCRIPT) {
      script_op *op = &ops[nops++];
      op->kind = arg[2] == 's' ? SC_SELECT_TAB : SC_CLOSE_TAB;
      op->x = atoi(NEXT());
    }
    else if(!strcmp(arg, "--tab")) {
      if(tab_new(a) < 0) {
        fprintf(stderr, "too many tabs\n");
        return 2;
      }
    }
    else if(!strcmp(arg, "--var") || !strcmp(arg, "--secret")) {
      const char *v = NEXT();
      const char *eq = strchr(v, '=');
      char name[VAR_NAME_CAP];
      if(!eq) {
        fprintf(stderr, "%s wants NAME=VALUE\n", arg);
        return 2;
      }
      snprintf(name, sizeof(name), "%.*s", (int)(eq - v), v);
      var_set(app_tab_env(a, CUR(a)), name, eq + 1, arg[2] == 's');
    }
    else if(!strcmp(arg, "--env")) {
      const char *name = NEXT();
      int k, found = -1;
      var_store *st = app_tab_store(a, CUR(a));
      for(k = 0; k < st->n; k++)
        if(!strcmp(st->e[k].name, name))
          found = k;
      if(found < 0)
        found = env_add(st, name);
      if(found >= 0)
        st->active = found;
    }
    else if(!strcmp(arg, "--form")) {
      static const char *const names[FORM_COUNT] = { "params", "headers", "body", "auth", "curl" };
      const char *v = NEXT();
      int k;
      for(k = 0; k < FORM_COUNT; k++)
        if(!strcmp(names[k], v))
          CUR(a)->form = (form_kind)k;
    }
    else if(!strcmp(arg, "--set-auth")) {
      const char *kind = NEXT();
      const char *x = NEXT();
      const char *y = NEXT();
      char err[128];
      auth_kind k = !strcmp(kind, "basic") ? AUTH_BASIC : !strcmp(kind, "bearer") ? AUTH_BEARER :
                    !strcmp(kind, "digest") ? AUTH_DIGEST : !strncmp(kind, "apikey", 6) ? AUTH_APIKEY : AUTH_NONE;
      if(form_auth_set(&CUR(a)->req, &CUR(a)->hints, k, x, y, !strcmp(kind, "apikey-query") ? "query" : "header", err,
                       sizeof(err))) {
        fprintf(stderr, "--set-auth: %s\n", err);
        return 2;
      }
    }
    else if(!strcmp(arg, "--set-header")) {
      const char *v = NEXT();
      const char *c = strchr(v, ':');
      kv_list rows;
      char name[256];
      form_headers_get(&CUR(a)->req, &CUR(a)->hints, &rows);
      snprintf(name, sizeof(name), "%.*s", c ? (int)(c - v) : (int)strlen(v), v);
      kv_add(&rows, name, c ? c + 2 : "", 1);
      form_headers_set(&CUR(a)->req, &CUR(a)->hints, &rows);
      kv_free(&rows);
    }
    else if(!strcmp(arg, "--set-body")) {
      /* raw, json and file take the text; urlencoded and form-data take a=b&c=d */
      const char *kind = NEXT();
      const char *v = NEXT();
      char err[128];
      body_kind k = !strcmp(kind, "raw") ? BODY_RAW : !strcmp(kind, "json") ? BODY_JSON : !strcmp(kind, "file") ? BODY_FILE :
                    !strcmp(kind, "urlencoded") ? BODY_URLENC : BODY_FORMDATA;
      kv_list rows;
      memset(&rows, 0, sizeof(rows));
      if(k == BODY_URLENC || k == BODY_FORMDATA) {
        const char *eq = strchr(v, '=');
        char name[256];
        kv_row *row;
        snprintf(name, sizeof(name), "%.*s", eq ? (int)(eq - v) : 0, v);
        row = kv_add(&rows, name, eq ? eq + 1 : v, 1);
        if(row && !strcmp(kind, "form-data-file"))
          row->is_file = 1;
      }
      if(form_body_set(&CUR(a)->req, k, &rows, v, err, sizeof(err))) {
        fprintf(stderr, "--set-body: %s\n", err);
        return 2;
      }
      kv_free(&rows);
    }
    else if(!strcmp(arg, "--set-param")) {
      const char *v = NEXT();
      const char *eq = strchr(v, '=');
      char name[256];
      if(!eq) {
        fprintf(stderr, "--set-param wants KEY=VALUE\n");
        return 2;
      }
      snprintf(name, sizeof(name), "%.*s", (int)(eq - v), v);
      form_params_sync(CUR(a));
      kv_add(&CUR(a)->params, name, eq + 1, 1);
      form_params_apply(CUR(a));
    }
    else if(!strcmp(arg, "--param-off")) {
      const char *v = NEXT();
      int k;
      for(k = 0; k < CUR(a)->params.n; k++)
        if(!strcmp(CUR(a)->params.r[k].key, v))
          CUR(a)->params.r[k].enabled = 0;
      form_params_apply(CUR(a));
    }
    else if(!strcmp(arg, "--save-as") && nops < MAX_SCRIPT) {
      script_op *op = &ops[nops++];
      op->kind = SC_SAVE_AS;
      op->text = NEXT();
    }
    else if(!strcmp(arg, "--open-node")) {
      coll_node *n = app_find_node(a, NEXT());
      if(n)
        app_open_node(a, n);
    }
    else if(!strcmp(arg, "--open-history")) {
      /* counted from the newest */
      int back = atoi(NEXT());
      if(tab_new(a) >= 0)
        app_open_history(a, a->hist.n - 1 - back);
    }
    else if(!strcmp(arg, "--import-cmd")) {
      char err[256];
      if(app_import_command(a, NEXT(), err, sizeof(err)))
        app_notice(a, err);
    }
    else if(!strcmp(arg, "--import-dialog"))
      dialog_open(a, DLG_IMPORT, NULL);
    else if(!strcmp(arg, "--native-frame"))
      a->set.native_frame = 1;
    else if(!strcmp(arg, "--quit"))
      a->quit = 1;
    else if(!strcmp(arg, "--set-body-off")) {
      const char *v = NEXT();
      const char *eq = strchr(v, '=');
      char name[256];
      snprintf(name, sizeof(name), "%.*s", eq ? (int)(eq - v) : 0, v);
      kv_add(&CUR(a)->body_rows, name, eq ? eq + 1 : v, 0);
    }
    else if(!strcmp(arg, "--sidebar")) {
      const char *v = NEXT();
      a->sidebar = !strcmp(v, "history") ? SIDE_HISTORY : SIDE_COLLECTIONS;
    }
    else if(!strcmp(arg, "--reveal"))
      CUR(a)->reveal = 1;
    else if(!strcmp(arg, "--open-vars"))
      app_open_vars(a, tab_coll(CUR(a)));
    else if((!strcmp(arg, "--modal-value") || !strcmp(arg, "--modal-action")) && nops < MAX_SCRIPT) {
      script_op *op = &ops[nops++];
      op->kind = arg[8] == 'v' ? SC_MODAL_VALUE : SC_MODAL_ACTION;
      op->text = NEXT();
    }
    else if(!strcmp(arg, "--no-wait"))
      no_wait = 1;
    else if(!strcmp(arg, "--paste-file") && nops < MAX_SCRIPT) {
      script_op *op = &ops[nops++];
      op->kind = SC_PASTE;
      op->text = NEXT();
    }
    else if(!strcmp(arg, "--type") && nops < MAX_SCRIPT) {
      script_op *op = &ops[nops++];
      op->kind = SC_TYPE;
      op->text = NEXT();
    }
    else {
      fprintf(stderr, "unknown argument: %s\n%s", arg, usage);
      return 2;
    }
  }
#undef NEXT

  SDL_SetHint(SDL_HINT_VIDEO_HIGHDPI_DISABLED, "0");
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");  /* crisp glyphs */
  SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
#ifdef __linux__
  /* XWayland blurs under a scaled desktop, so a Wayland session gets the native backend.
     GNOME draws no title bars for clients: libdecor does, and our own bar needs neither. */
  if(getenv("WAYLAND_DISPLAY") && !getenv("SDL_VIDEODRIVER")) {
    void *decor;
    /* An AppImage's libdecor is older than the system's plugins and cannot load them, and SDL
       has it from the start. So it starts once more with the system's in its place: a preload
       is the only thing that comes first. It is undone at the top of main, curl must not get it. */
    if(a->set.native_frame && getenv("APPIMAGE") && !decor_restarted) {
      char lib[SETTINGS_PATH_CAP], plugins[SETTINGS_PATH_CAP];
      if(libdecor_find(lib, sizeof(lib), plugins, sizeof(plugins))) {
        const char *was = getenv("LD_PRELOAD");
        char preload[SETTINGS_PATH_CAP * 2];
        snprintf(preload, sizeof(preload), "%s%s%s", lib, was && *was ? ":" : "", was ? was : "");
        SDL_setenv(DECOR_RESTART_ENV, was ? was : "", 1);
        SDL_setenv("LD_PRELOAD", preload, 1);
        SDL_setenv("LIBDECOR_PLUGIN_DIR", plugins, 1);
        execv("/proc/self/exe", argv);
      }
      no_system_bar = 1;
    }
    decor = SDL_LoadObject("libdecor-0.so.0");
    if(decor) {
      SDL_UnloadObject(decor);
      SDL_setenv("SDL_VIDEODRIVER", "wayland", 1);  /* the hint needs SDL 2.0.22 */
    }
  }
#endif
  if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
    SDL_setenv("SDL_VIDEODRIVER", "", 1);
    SDL_ClearError();
    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
      fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
      return 1;
    }
  }
  if(!win_size_given && a->set.win_w >= 400 && a->set.win_h >= 300) {
    win_w = a->set.win_w;
    win_h = a->set.win_h;
  }
  if(win_w < MIN_W && !screenshot)
    win_w = MIN_W;
  if(win_h < MIN_H && !screenshot)
    win_h = MIN_H;
#ifndef __APPLE__
  /* A Mac keeps its own buttons, where people expect them. */
  a->own_frame = !a->set.native_frame || no_system_bar;
  if(no_system_bar)
    app_notice(a, "This system has no libdecor with plugins to draw its title bar, so the app's own buttons stay.");
#endif
  g_window = SDL_CreateWindow("curl UI", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, win_w, win_h,
                              SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | (a->own_frame ? SDL_WINDOW_BORDERLESS : 0) |
                              (screenshot ? SDL_WINDOW_HIDDEN : SDL_WINDOW_SHOWN));
  if(!g_window) {
    fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
    return 1;
  }
  SDL_SetWindowMinimumSize(g_window, MIN_W, MIN_H);
  /* Where a system cannot do this, our bar could not move the window: its own bar comes back.
     The dummy driver of the tests has neither, and nobody to drag it. */
  if(a->own_frame && SDL_SetWindowHitTest(g_window, hit_test, a) && strcmp(SDL_GetCurrentVideoDriver(), "dummy")) {
    if(g_trace)
      fprintf(stderr, "no hit test: %s\n", SDL_GetError());
    a->own_frame = 0;
    SDL_SetWindowBordered(g_window, SDL_TRUE);
  }
  if(screenshot)
    render_flags = SDL_RENDERER_SOFTWARE;
  g_renderer = SDL_CreateRenderer(g_window, -1, render_flags);
  if(!g_renderer)
    g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_SOFTWARE);
  if(!g_renderer) {
    fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
    return 1;
  }
  SDL_GetRendererOutputSize(g_renderer, &ow, &oh);
  SDL_GetWindowSize(g_window, &ww, &wh);
  g_scale = (float)ow / (float)ww;
  g_zoom = 1.0f;
  if(g_trace) {
    SDL_RendererInfo ri;
    if(!SDL_GetRendererInfo(g_renderer, &ri))
      fprintf(stderr, "renderer %s\n", ri.name);
  }
  if(g_trace)
    fprintf(stderr, "driver %s window %dx%d output %dx%d scale %.2f\n", SDL_GetCurrentVideoDriver(), ww, wh, ow, oh,
            (double)g_scale);

  a->ctx = nk_sdl_init(g_window, g_renderer);
  ui_install_paste(a);
  {
    Uint32 t0 = SDL_GetTicks();
    load_fonts(a);
    if(stats)
      printf("fonts baked in %u ms\n", (unsigned)(SDL_GetTicks() - t0));
  }
  theme_apply(a, light ? 0 : a->set.dark);
  g_cursors[CURSOR_ARROW] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
  g_cursors[CURSOR_HAND] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
  g_cursors[CURSOR_IBEAM] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_IBEAM);
  g_cursors[CURSOR_NS] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENS);
  g_cursors[CURSOR_EW] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZEWE);
  g_cursors[CURSOR_NWSE] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENWSE);
  g_cursors[CURSOR_NESW] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENESW);

  app_detect_curl(a);
  if(method) {
    for(i = 1; i < METHOD_COUNT; i++)
      if(!strcmp(app_method_label(a, CUR(a), i), method))
        CUR(a)->method = i;
  }
  if(search)
    snprintf(a->search, sizeof(a->search), "%s", search);
  if(docs) {
    int d = options_find(docs);
    if(d < 0) {
      fprintf(stderr, "unknown option: %s\n", docs);
      return 2;
    }
    a->doc_opt = d;
  }
  if(want_log)
    CUR(a)->out_tab = TAB_LOG;

  settle(a);
  {
    int keep = a->cur, t;
    for(t = 0; t < a->ntabs; t++) {
      if(a->tabs[t]->run_at_start) {
        a->cur = t;
        app_run(a);
      }
    }
    a->cur = keep;
  }
  if(!no_wait) {
    wait_run(a);
    /* the run may switch tabs: an explicit --log still wins */
    if(want_log)
      CUR(a)->out_tab = TAB_LOG;
  }
  run_script(a, ops, nops);
  if(!no_wait)
    wait_run(a);

  if(screenshot) {
    int rc;
    input_frame(a, NULL, NULL);
    if(dump && write_dump(a, dump))
      fprintf(stderr, "cannot write %s\n", dump);
    if(a->settings_path[0]) {
      settings_save(&a->set, a->settings_path);
      app_vars_flush(a);
      app_session_save(a);
    }
    /* render once more to a presented-free back buffer and read it */
    rc = save_screenshot(screenshot);
    if(rc)
      fprintf(stderr, "screenshot failed: %s\n", SDL_GetError());
    if(stats)
      printf("frames %ld, UI time avg %.3f ms, max %.3f ms\n", g_frames, g_frame_ms_total / (double)g_frames, g_frame_ms_max);
    app_free(a);
    nk_sdl_shutdown();
    SDL_DestroyRenderer(g_renderer);
    SDL_DestroyWindow(g_window);
    SDL_Quit();
    return rc ? 1 : 0;
  }
  SDL_RenderPresent(g_renderer);
  SDL_StartTextInput();  /* not implied by every SDL backend */
  saved = a->set;

  /* Event driven: asleep until something happens, except while curl runs, a
     hover is settling, or a caret needs blinking. */
  while(!a->quit) {
    SDL_Event ev;
    int got, transition;
    int timeout = -1;
    if(need_frames > 0)
      timeout = 0;
    else if(app_any_running(a) || a->wants_timer || a->drag_scroll)
      timeout = 30;
    else if(a->any_focus)
      timeout = 500;

    if(have_held) {
      ev = held;
      got = 1;
      have_held = 0;
    }
    else
      got = timeout < 0 ? SDL_WaitEvent(&ev) : SDL_WaitEventTimeout(&ev, timeout);
    nk_input_begin(a->ctx);
    transition = 0;
    while(got) {
      /* Nuklear reads "pressed" from the state at the end of a frame, so a
         press and its release in one frame are a press never seen: a quick
         click would not focus a field, a quick key tap would be lost. One
         button or key transition per frame; the next waits for its own. */
      int is_transition = ev.type == SDL_MOUSEBUTTONDOWN || ev.type == SDL_MOUSEBUTTONUP;
      /* plain typing arrives as text, its key events mean nothing to Nuklear:
         splitting those too would make fast typing lag behind */
      if((ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP) &&
         (ev.key.keysym.sym < 0x20 || ev.key.keysym.sym >= 0x7f || (ev.key.keysym.mod & (KMOD_CTRL | KMOD_GUI))))
        is_transition = 1;
      if(is_transition && transition) {
        held = ev;
        have_held = 1;
        need_frames = 2;
        break;
      }
      transition |= is_transition;
      if(g_trace) {
        int tw, th, to, tp;
        SDL_GetWindowSize(g_window, &tw, &th);
        SDL_GetRendererOutputSize(g_renderer, &to, &tp);
        fprintf(stderr, "event %#x window %dx%d output %dx%d", ev.type, tw, th, to, tp);
        if(ev.type == SDL_MOUSEMOTION)
          fprintf(stderr, " motion %d,%d", ev.motion.x, ev.motion.y);
        else if(ev.type == SDL_MOUSEBUTTONDOWN || ev.type == SDL_MOUSEBUTTONUP)
          fprintf(stderr, " button %d,%d", ev.button.x, ev.button.y);
        else if(ev.type == SDL_TEXTINPUT)
          fprintf(stderr, " text %s", ev.text.text);
        else if(ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP)
          fprintf(stderr, " key sym %#x scan %d", (unsigned)ev.key.keysym.sym, (int)ev.key.keysym.scancode);
        else if(ev.type == SDL_WINDOWEVENT)
          fprintf(stderr, " window-event %d", ev.window.event);
        fprintf(stderr, "\n");
      }
      if(ev.type == SDL_QUIT) {
        int dirty = 0, k;
        for(k = 0; k < a->ntabs; k++)
          dirty |= a->tabs[k]->dirty;
        if(dirty && !a->set.reopen_tabs && a->dlg == DLG_NONE)
          dialog_open(a, DLG_QUIT, NULL);
        else
          a->quit = 1;
      }
      if(ev.type == SDL_KEYDOWN && (ev.key.keysym.mod & (KMOD_CTRL | KMOD_GUI)) && !a->modal_open && a->dlg == DLG_NONE && !a->picker.open) {
        if(ev.key.keysym.sym == SDLK_s)
          app_save_current(a);
        else if(ev.key.keysym.sym == SDLK_t) {
          CUR(a)->reveal = 0;
          if(tab_new(a) >= 0)
            a->pending_unfocus = 1;
        }
        else if(ev.key.keysym.sym == SDLK_w)
          app_request_close(a, a->cur);
        else if(ev.key.keysym.sym == SDLK_RETURN && !CUR(a)->proc)
          app_run(a);
      }
      if(ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE && a->picker.open)
        a->picker.pending = 2;
      /* the UI is laid out in zoomed units, SDL reports window units */
      if(ev.type == SDL_MOUSEMOTION) {
        ev.motion.x = (Sint32)((float)ev.motion.x / g_zoom);
        ev.motion.y = (Sint32)((float)ev.motion.y / g_zoom);
      }
      else if(ev.type == SDL_MOUSEBUTTONDOWN || ev.type == SDL_MOUSEBUTTONUP) {
        ev.button.x = (Sint32)((float)ev.button.x / g_zoom);
        ev.button.y = (Sint32)((float)ev.button.y / g_zoom);
      }
      nk_sdl_handle_event(&ev);
      need_frames = 2;  /* Nuklear settles hover and focus a frame later */
      got = SDL_PollEvent(&ev);
    }
    nk_input_end(a->ctx);
    if(need_frames > 0)
      need_frames--;
    frame(a);
    SDL_RenderPresent(g_renderer);
    if(a->reset_settings) {
      settings_defaults(&a->set);
      theme_apply(a, a->set.dark);
      a->pending_unfocus = 1;
      a->reset_settings = 0;
      app_detect_curl(a);
      a->doc_built_opt = -2;
    }
    /* saved when something changed, but not per keystroke */
    if(a->settings_path[0] && !a->any_focus && memcmp(&saved, &a->set, sizeof(saved))) {
      settings_save(&a->set, a->settings_path);
      saved = a->set;
    }
  }

  if(a->settings_path[0]) {
    SDL_GetWindowSize(g_window, &a->set.win_w, &a->set.win_h);
    settings_save(&a->set, a->settings_path);
    app_vars_flush(a);
    app_session_save(a);
  }
  if(stats)
    printf("frames %ld, UI time avg %.3f ms, max %.3f ms\n", g_frames, g_frame_ms_total / (double)g_frames, g_frame_ms_max);
  app_free(a);
  nk_sdl_shutdown();
  for(i = 0; i < CURSOR_COUNT; i++)
    if(g_cursors[i])
      SDL_FreeCursor(g_cursors[i]);
  SDL_DestroyRenderer(g_renderer);
  SDL_DestroyWindow(g_window);
  SDL_Quit();
  return 0;
}
