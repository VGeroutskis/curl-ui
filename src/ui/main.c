/* curl UI: the SDL window, event loop, and the scripted test hooks. */
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
static SDL_Cursor *g_cursors[3];
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
  apply_cursor(a->cursor);

  SDL_SetRenderDrawColor(g_renderer, a->pal.bg.r, a->pal.bg.g, a->pal.bg.b, 255);
  SDL_RenderClear(g_renderer);
  SDL_RenderSetScale(g_renderer, a->W > 0 ? (float)ow / a->W : 1, a->H > 0 ? (float)oh / a->H : 1);
  nk_sdl_render(NK_ANTI_ALIASING_ON);
  SDL_RenderSetScale(g_renderer, 1, 1);
}

/* ---- scripted test hooks ---- */

typedef enum { SC_MOUSE, SC_CLICK, SC_TYPE, SC_WHEEL } script_kind;
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
  while(a->proc && SDL_GetTicks() - t0 < 20000) {
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
    case SC_WHEEL:
      input_frame(a, feed_motion, op);
      input_frame(a, feed_wheel, op);
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
  "  --run               run the request at startup\n"
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
  "  --stats             print UI time per frame on exit\n";

int main(int argc, char **argv)
{
  app_t *a = &g_app;
  script_op ops[MAX_SCRIPT];
  int win_size_given = 0;
  int nops = 0, want_run = 0, want_log = 0, light = 0, stats = 0, win_w = 1360, win_h = 840, i;
  const char *settings_dir = NULL;
  char request_path[SETTINGS_PATH_CAP] = "";
  int have_cli_request = 0;
  settings_t saved;
  const char *screenshot = NULL, *docs = NULL, *search = NULL, *method = NULL;
  int ow, oh, ww, wh;
  int need_frames = 3;
  SDL_Event held;
  int have_held = 0;
  Uint32 render_flags = SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC;

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
    settings_load(&a->set, a->settings_path);
    settings_path(settings_dir, "last-request.txt", request_path, sizeof(request_path));
    if(a->set.remember && !have_cli_request)
      request_load(&a->req, &a->method, request_path);
  }
  a->pretty = a->set.pretty;
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
    else if(!strcmp(arg, "--url"))
      snprintf(a->req.url, sizeof(a->req.url), "%s", NEXT());
    else if(!strcmp(arg, "--method"))
      method = NEXT();
    else if(!strcmp(arg, "--run"))
      want_run = 1;
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
    else if(!strcmp(arg, "--wheel") && nops < MAX_SCRIPT) {
      script_op *op = &ops[nops++];
      op->kind = SC_WHEEL;
      if(sscanf(NEXT(), "%d,%d,%d", &op->x, &op->y, &op->dy) != 3) {
        fprintf(stderr, "--wheel wants X,Y,DELTA\n");
        return 2;
      }
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
  /* XWayland blurs under a scaled desktop, so a Wayland session gets the native
     backend. GNOME draws no title bars for clients, so only with libdecor. */
  if(getenv("WAYLAND_DISPLAY") && !getenv("SDL_VIDEODRIVER")) {
    void *decor = SDL_LoadObject("libdecor-0.so.0");
    if(decor) {
      SDL_UnloadObject(decor);
      SDL_SetHint(SDL_HINT_VIDEODRIVER, "wayland");
    }
  }
#endif
  if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
    SDL_SetHint(SDL_HINT_VIDEODRIVER, "");
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
  g_window = SDL_CreateWindow("curl UI", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, win_w, win_h,
                              SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | (screenshot ? SDL_WINDOW_HIDDEN : SDL_WINDOW_SHOWN));
  if(!g_window) {
    fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
    return 1;
  }
  SDL_SetWindowMinimumSize(g_window, MIN_W, MIN_H);
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

  app_detect_curl(a);
  if(method) {
    for(i = 1; i < METHOD_COUNT; i++)
      if(!strcmp(app_method_label(a, i), method))
        a->method = i;
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
    a->tab = TAB_LOG;

  settle(a);
  if(want_run) {
    app_run(a);
    wait_run(a);
    /* the run may switch tabs: an explicit --log still wins */
    if(want_log)
      a->tab = TAB_LOG;
  }
  run_script(a, ops, nops);
  wait_run(a);

  if(screenshot) {
    int rc;
    input_frame(a, NULL, NULL);
    if(a->settings_path[0]) {
      settings_save(&a->set, a->settings_path);
      if(a->set.remember)
        request_save(&a->req, a->method, request_path);
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
    else if(a->proc || a->wants_timer || a->drag_scroll)
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
      if(ev.type == SDL_QUIT)
        a->quit = 1;
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
    if(a->set.remember)
      request_save(&a->req, a->method, request_path);
    else
      remove(request_path);  /* turning it off forgets what it held */
  }
  if(stats)
    printf("frames %ld, UI time avg %.3f ms, max %.3f ms\n", g_frames, g_frame_ms_total / (double)g_frames, g_frame_ms_max);
  app_free(a);
  nk_sdl_shutdown();
  for(i = 0; i < 3; i++)
    if(g_cursors[i])
      SDL_FreeCursor(g_cursors[i]);
  SDL_DestroyRenderer(g_renderer);
  SDL_DestroyWindow(g_window);
  SDL_Quit();
  return 0;
}
