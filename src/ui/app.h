/* Application state and the views over it. */
#ifndef CURLUI_APP_H
#define CURLUI_APP_H

#include <limits.h>

#include "nk.h"

#include "argfile.h"
#include "builder.h"
#include "collection.h"
#include "curlinfo.h"
#include "docs.h"
#include "forms.h"
#include "history.h"
#include "mask.h"
#include "options.h"
#include "outbuf.h"
#include "settings.h"
#include "urlquery.h"
#include "vars.h"
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

#define MAX_TABS 32

typedef enum { FORM_PARAMS, FORM_HEADERS, FORM_BODY, FORM_AUTH, FORM_CURL, FORM_COUNT } form_kind;

/* One open request: what it asks, and what its run produced. */
typedef struct {
  request req;
  int method;           /* index into the method menu; 0 lets curl decide */
  arg_list args_tmpl;   /* with {{name}} */
  arg_list args_real;   /* what curl gets */
  arg_list args;        /* what is on screen: secrets hidden, tokens parallel to the others */
  int dirty;            /* differs from what is saved */
  char node_id[17];     /* the saved request it is, or "" */
  char title[128];
  char *saved_json;     /* what was last saved, to compare with */
  unsigned long dirty_sig;
  jv *extra;            /* options this build does not know, carried along */
  jv *run_req;          /* the request as written when this run began, for history */
  char run_method[16];
  form_kind form;
  kv_list params;       /* the query of the URL, and rows switched off */
  char *params_url;     /* the URL params was last read from or written to */
  float form_scroll;
  int form_focus;
  form_hints hints;
  unsigned long form_sig;  /* the request the rows below were read from */
  kv_list header_rows, body_rows;
  body_kind body_choice;
  char *body_file;
  size_t body_file_cap;
  char *auth_a, *auth_b;
  size_t auth_a_cap, auth_b_cap;
  int auth_query;
  char auth_error[128];
  int reveal;           /* the eye on the command: real values, until the tab changes */
  unsigned long copied_secret_at;  /* when Copy took real secret values, for a short note */
  mask_set *mask;       /* the secrets of the last run, hidden in its output */
  int run_at_start;     /* scripted: --run */

  /* centre */
  int show_recipes;
  int method_menu;
  int url_missing;      /* Run was pressed with nothing to fetch */
  nk_uint cards_x, cards_y;
  int cards_focus;
  int cards_focus_prev;
  nk_uint cards_lock_y;
  float out_scroll;
  int out_follow;
  out_tab out_tab;
  int pretty;

  /* run */
  proc_t *proc;
  arg_files run_files;  /* the arguments, when too long for a command line */
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
} tab_t;

#define CUR(a) ((a)->tabs[(a)->cur])

typedef enum { SIDE_COLLECTIONS, SIDE_HISTORY, SIDE_OPTIONS, SIDE_COUNT } side_kind;
typedef enum {
  DLG_NONE, DLG_NEW_COLL, DLG_NEW_FOLDER, DLG_NEW_REQUEST, DLG_RENAME, DLG_SAVE_AS, DLG_MOVE, DLG_DELETE, DLG_CLOSE_TAB,
  DLG_QUIT, DLG_IMPORT
} dialog_kind;

typedef enum { MODAL_NONE, MODAL_SAVE, MODAL_CANCEL, MODAL_OPEN_VARS } modal_action;

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
  int big_paste;        /* the focused field was asked to take a paste too big to edit */
  int quit;

  /* model */
  tab_t *tabs[MAX_TABS];
  int ntabs, cur;
  settings_t set;
  char settings_path[SETTINGS_PATH_CAP];   /* "" means nothing is persisted */
  float settings_scroll;
  int settings_focus;
  int reset_settings;
  curl_info curl;

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

  int show_settings;

  /* collections */
  side_kind sidebar;
  collection **colls;
  int ncolls;
  char coll_dir[SETTINGS_PATH_CAP];
  char session_path[SETTINGS_PATH_CAP];
  char coll_search[128];
  float tree_scroll;
  coll_node *tree_menu;
  struct nk_vec2 tree_menu_at;
  dialog_kind dlg;
  coll_node *dlg_node;
  coll_node **dlg_targets;
  int dlg_ntargets, dlg_target;
  float dlg_scroll;
  char dlg_name[256];
  int dlg_pending;      /* 1 ok, 2 cancel, 3 discard */
  int dlg_close_after;  /* a Save as that started from closing the tab */
  char *import_text;
  size_t import_cap;
  char import_err[256];

  /* history */
  history hist;
  char hist_path[SETTINGS_PATH_CAP];
  float hist_scroll;
  int hist_clear_armed;

  /* variables */
  var_store vars;
  char vars_path[SETTINGS_PATH_CAP];
  unsigned long vars_hash;
  int show_vars;
  float vars_scroll;
  int vars_focus;
  int env_menu;
  struct nk_rect env_btn;
  char env_edit[128];
  int env_edit_for;
  int env_editing;
  int env_del_armed;
  char notice[2048];    /* something the user should know, until dismissed */

  /* the prompt for missing variables */
  int modal_open;
  modal_action modal_pending;
  str_list modal_names;
  char **modal_values;
  size_t *modal_caps;
  int *modal_secret;

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
void app_poll(app_t *a);       /* notices the end of a run, in any tab */
int app_any_running(const app_t *a);
int tab_new(app_t *a);         /* a slot made current, or -1 at MAX_TABS */
void tab_close(app_t *a, int i);  /* stops its run first; the last tab leaves a fresh one */
void app_rebuild_args(app_t *a);
void app_apply_actions(app_t *a);
void app_push(app_t *a, act_kind kind, int x, int y);
void app_set_doc(app_t *a, int opt, int push_history);
void app_doc_back(app_t *a);
const char *app_method_label(const app_t *a, const tab_t *t, int index);
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
/* The same over a growing buffer: room for the text, what is typed in a frame, and a paste. */
nk_flags ui_edit_dyn(app_t *a, struct nk_rect r, char **buf, size_t *cap, const char *placeholder,
                     const struct nk_user_font *font, nk_flags extra);
/* Text past this is not edited in place: Nuklear lays out every character each frame,
   and a focused field past about 16000 glyphs overflows its 16-bit vertex indices. */
#define FIELD_EDIT_MAX 8192u
void ui_install_paste(app_t *a);
void ui_hover_doc(app_t *a, int opt);
size_t utf8_next(const char *s, size_t len, size_t i);
void ui_copy(const char *text);

/* The eye of a secret. Returns 1 when clicked. */
int ui_eye(app_t *a, struct nk_rect r, int open);

/* view_modal.c, view_vars.c */
void modal_open_missing(app_t *a, str_list *names);  /* takes names */
void modal_close(app_t *a);
void modal_act(app_t *a, modal_action act);
void view_modal(app_t *a);
void view_vars(app_t *a, struct nk_rect r);
void app_vars_save(app_t *a);
void app_vars_autosave(app_t *a);
unsigned long app_vars_hash(const app_t *a);
struct nk_rect env_menu_rect(app_t *a);
int env_menu_item_at(app_t *a, struct nk_vec2 pos);
void env_menu_pick(app_t *a, int item);
void env_menu_draw(app_t *a);
void app_notice(app_t *a, const char *text);

/* store.c, view_collections.c */
void tab_mark_clean(tab_t *t);
void app_tabs_update_dirty(app_t *a);
void app_open_node(app_t *a, coll_node *n);
coll_node *app_find_node(app_t *a, const char *id);
int app_save_coll_of(app_t *a, const coll_node *n);
collection *app_new_collection(app_t *a, const char *name);
int tab_save_to_node(app_t *a, tab_t *t, coll_node *n);
void app_save_current(app_t *a);
int app_save_as_path(app_t *a, const char *path);
void app_history_begin(app_t *a, tab_t *t);
void app_history_end(app_t *a, tab_t *t);
void app_open_history(app_t *a, int index);
int app_import_command(app_t *a, const char *text, char *err, size_t errn);  /* into a new or blank tab */
void app_store_load(app_t *a);
void app_session_load(app_t *a);
void app_session_save(app_t *a);
void app_store_free(app_t *a);
void app_request_close(app_t *a, int i);
void dialog_open(app_t *a, dialog_kind kind, coll_node *node);
void view_dialog(app_t *a);
void dialog_run_pending(app_t *a);
void tree_menu_click(app_t *a, struct nk_vec2 pos);
void tree_menu_draw(app_t *a);
void view_sidebar(app_t *a, struct nk_rect r);
void view_history(app_t *a, struct nk_rect r);
unsigned long form_request_sig(const request *q);

/* views */
void view_frame(app_t *a);
void view_tabs(app_t *a, struct nk_rect r);
void view_form(app_t *a, struct nk_rect r);
void form_params_sync(tab_t *t);
void form_params_apply(tab_t *t);
int form_kv_table(app_t *a, struct nk_rect r, kv_list *l, const char *key_hint, const char *value_hint, int doc_opt,
                  int file_col);
void view_header(app_t *a, struct nk_rect r);
void view_library(app_t *a, struct nk_rect r);
void view_center(app_t *a, struct nk_rect r);
void view_output(app_t *a, struct nk_rect r);
void view_docs(app_t *a, struct nk_rect r);
void view_settings(app_t *a, struct nk_rect r);

#endif
