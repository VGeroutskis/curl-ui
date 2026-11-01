/* Application state and the views over it. */
#ifndef CURLUI_APP_H
#define CURLUI_APP_H

#include <limits.h>

#include "nk.h"

#include "builder.h"
#include "curlinfo.h"
#include "docs.h"
#include "options.h"
#include "outbuf.h"
#include "settings.h"
#include "procrun.h"

typedef struct {
  struct nk_color bg, panel, card, border, border_strong;
  struct nk_color text, dim, faint;
  struct nk_color accent, accent_hover, accent_soft, on_accent;
  struct nk_color green, amber, amber_bg, red;
  struct nk_color field, hover, code_bg;
  struct nk_color j_key, j_str, j_num, j_lit;
} palette;

typedef struct {
  const struct nk_user_font *sans, *semi, *small, *title, *mono, *mono_semi, *mono_small;
  float mono_w, mono_small_w;  /* monospace advance */
} fonts_t;

typedef enum { TAB_RESPONSE, TAB_LOG } out_tab;
typedef enum { CURSOR_ARROW, CURSOR_HAND, CURSOR_IBEAM } cursor_kind;

typedef struct {
  unsigned line, off, len;
} wrap_row;

/* Incremental soft-wrap index over an outbuf. */
typedef struct {
  outbuf *src;
  int cols;
  size_t lines_done;
  unsigned long gen;
  wrap_row *rows;
  size_t nrows, cap;
} wrap_cache;

typedef enum {
  ACT_NONE, ACT_TOGGLE, ACT_REMOVE, ACT_ADD_VALUE, ACT_REMOVE_VALUE, ACT_SET_NEG,
  ACT_CLEAR, ACT_RECIPE, ACT_RUN, ACT_STOP, ACT_USE_AS_URL
} act_kind;

typedef struct {
  act_kind kind;
  int a, b;
} action;

#define MAX_ACTIONS 16
#define DOC_HISTORY 64
#define SEARCH_CAP 256

/* A laid out piece of the docs panel. */
typedef enum {
  DOP_TEXT, DOP_RECT, DOP_LINK, DOP_CHIP, DOP_PILL
} dop_kind;

typedef struct {
  dop_kind kind;
  float x, y, w, h;
  const struct nk_user_font *font;
  int color;        /* palette role, see view_docs.c */
  const char *s;
  int len;
  int opt;          /* link target or -1 */
} dop;

typedef struct {
  /* platform */
  struct nk_context *ctx;
  fonts_t f;
  palette pal;
  int dark;
  float W, H;
  cursor_kind cursor;
  unsigned long now_ms;
  int blink_off;
  int wants_timer;      /* something is waiting on the clock */
  int any_focus;
  int pending_unfocus;  /* actions run outside the window; applied in the next */
  int quit;

  /* model */
  request req;
  int method;           /* index into the method menu; 0 lets curl decide */
  settings_t set;
  char settings_path[SETTINGS_PATH_CAP];   /* "" means nothing is persisted */
  float settings_scroll;
  int settings_focus;
  int reset_settings;
  curl_info curl;
  arg_list args;

  /* library */
  char search[SEARCH_CAP];
  char search_last[SEARCH_CAP];
  int filter;           /* chip index */
  int filter_last;
  int show_more;
  int *results;
  int nresults;
  float lib_scroll;
  int results_valid;
  int show_all;         /* also list what cannot combine with the selection */
  int nhidden;
  unsigned long sel_key;
  int sel_scratch[512];

  /* centre */
  int show_recipes;
  int method_menu;
  int show_settings;
  int url_missing;      /* Run was pressed with nothing to fetch */
  nk_uint cards_x, cards_y;
  int cards_focus;
  int cards_focus_prev;
  nk_uint cards_lock_y;
  float out_scroll;
  int out_follow;
  out_tab tab;
  int pretty;

  /* run */
  proc_t *proc;
  outbuf *out, *log, *pretty_buf;
  unsigned long run_started;
  float run_secs;
  int has_result, exit_code;
  int http_status;
  double curl_time;
  long long curl_size;
  int has_stats;
  int pretty_valid;
  size_t pretty_header_lines;
  wrap_cache wrap_out, wrap_log, wrap_pretty;

  /* docs */
  int doc_opt;
  int doc_hist[DOC_HISTORY];
  int doc_hist_n;
  int hover_frame;
  int hover_cand;
  unsigned long hover_since;
  float doc_scroll;
  float doc_content_h;
  doc_t doc;
  int doc_built_opt;
  int doc_built_flags;
  float doc_built_w;
  dop *dops;
  int ndops, capdops;
  char *doc_arena;
  size_t doc_arena_n;

  /* scrollbar being dragged */
  float *drag_scroll;
  float drag_grab;

  action actions[MAX_ACTIONS];
  int nactions;
} app_t;

/* app.c */
void app_init(app_t *a);
void app_free(app_t *a);
void app_detect_curl(app_t *a);
void app_add_option(app_t *a, const char *name, const char *value);
void app_toggle_option(app_t *a, int opt);
void app_apply_recipe(app_t *a, int index);
void app_run(app_t *a);
void app_stop(app_t *a);
void app_poll(app_t *a);       /* notices the end of a run */
void app_rebuild_args(app_t *a);
void app_apply_actions(app_t *a);
void app_push(app_t *a, act_kind kind, int x, int y);
void app_set_doc(app_t *a, int opt, int push_history);
void app_doc_back(app_t *a);
const char *app_method_label(const app_t *a, int index);
int app_option_supported(const app_t *a, int opt);
void app_clear_output(app_t *a);
void wrap_free(wrap_cache *w);

#define METHOD_COUNT 8
typedef struct {
  const char *title, *desc, *method, *url;
  struct { const char *name, *value; } opts[5];
} recipe;
extern const recipe g_recipes[];
extern const int g_recipe_count;

/* theme.c */
void theme_apply(app_t *a, int dark);

/* draw.c: helpers shared by the views. g_ui is the frame's clip context. */
#define UI_TRANSPARENT nk_rgba(0, 0, 0, 0)
extern struct nk_rect g_clip;
int ui_hover(app_t *a, struct nk_rect r);
int ui_clicked(app_t *a, struct nk_rect r);
float ui_text_w(const struct nk_user_font *f, const char *s, int len);
void ui_text(app_t *a, const struct nk_user_font *f, float x, float y, const char *s, int len,
             struct nk_color c);
/* Single line, cut with an ellipsis to max_w. Vertically centred in h. */
void ui_text_fit(app_t *a, const struct nk_user_font *f, float x, float y, float h, float max_w,
                 const char *s, struct nk_color c);
void ui_rect(app_t *a, struct nk_rect r, float round, struct nk_color fill);
void ui_stroke(app_t *a, struct nk_rect r, float round, struct nk_color c);
void ui_push_clip(app_t *a, struct nk_rect r);
struct nk_rect ui_intersect(struct nk_rect p, struct nk_rect q);
/* Text button or link; returns 1 when clicked. */
typedef enum { BTN_PRIMARY, BTN_SECONDARY, BTN_GHOST, BTN_DANGER } btn_style;
int ui_button(app_t *a, struct nk_rect r, const char *label, btn_style style);
float ui_pill(app_t *a, float x, float y, const char *label, struct nk_color fg, struct nk_color bg);
float ui_pill_w(app_t *a, const char *label);
void ui_scrollbar(app_t *a, struct nk_rect track, float *scroll, float content);
void ui_clamp_scroll(float *scroll, float content, float view);
int ui_wheel(app_t *a, struct nk_rect r, float *scroll, float content, float step);
nk_flags ui_edit(app_t *a, struct nk_rect r, char *buf, int cap, const char *placeholder,
                 const struct nk_user_font *font, nk_flags extra);
void ui_hover_doc(app_t *a, int opt);
size_t utf8_next(const char *s, size_t len, size_t i);
void ui_copy(const char *text);

/* views */
void view_frame(app_t *a);
void view_header(app_t *a, struct nk_rect r);
void view_library(app_t *a, struct nk_rect r);
void view_center(app_t *a, struct nk_rect r);
void view_output(app_t *a, struct nk_rect r);
void view_docs(app_t *a, struct nk_rect r);
void view_settings(app_t *a, struct nk_rect r);

#endif
