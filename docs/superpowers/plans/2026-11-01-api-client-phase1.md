# curl UI ως API client, φάση 1: Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Tabs, μεταβλητές με secrets, φόρμες, συλλογές, ιστορικό και εισαγωγή εντολής curl, πάνω στο υπάρχον μοντέλο επιλογών, για την έκδοση `0.4.0`.

**Architecture:** Οι φόρμες είναι όψεις των επιλογών του `request`. Η αντικατάσταση μεταβλητών γίνεται πάνω στο `arg_list` που βγάζει το `builder`, και παράγει δύο λίστες: την πραγματική για το curl και την καλυμμένη για την οθόνη. Η λογική μπαίνει σε modules του `src/core` χωρίς SDL, και οι όψεις στο `src/ui`.

**Tech Stack:** C11, SDL2, Nuklear, CMake/CTest. Καμία νέα εξωτερική βιβλιοθήκη.

**Spec:** `docs/superpowers/specs/2026-11-01-api-client-phase1-design.md`

## Global Constraints

- Commit χωρίς `Co-Authored-By` ή άλλη αναφορά σε Claude. Το push γίνεται στο `main` του `VGeroutskis/curl-ui`. Tag μόνο με ρητή εντολή του χρήστη.
- C11, `-Wall -Wextra` χωρίς warnings, χωρίς SDL στο `src/core`.
- Το curl τρέχει μόνο με πίνακα ορισμάτων, ποτέ μέσω shell. Το λειτουργικό δεν δίνει και δεν λαμβάνει τιμές μεταβλητών (`getenv`/`setenv` απαγορεύονται στον κώδικα των μεταβλητών).
- Όλα τα νέα αρχεία δεδομένων: `file_write_private` (ατομικά, 0600).
- Κάθε task: `cmake --build build -j && ctest --test-dir build --output-on-failure` καθαρό, και πράσινο CI σε Linux, Windows, macOS πριν από το επόμενο task. Το build με `-DCURLUI_SANITIZE=ON -DCURLUI_BUILD_GUI=OFF` πρέπει επίσης να περνά.
- Τα νέα τεστ του πυρήνα μπαίνουν στο `tests/test_api.c`, με τα ίδια `CHECK` και `CHECK_STR` που έχει το `tests/test_core.c`, και δηλώνονται στο `CMakeLists.txt` ως `add_test(NAME api ...)`.
- Το UI δοκιμάζεται με τα hooks του `src/ui/main.c`. Κάθε νέο hook τεκμηριώνεται στον πίνακα Command line του README.

## Review Focus

1. **Μεγάλη τιμή μεταβλητής:** μια τιμή που κάνει το όρισμα μεγαλύτερο από 16 KB πρέπει να φτάνει ολόκληρη στο curl. Τεστ στο Task 3. Η σχεδίαση πάνω στο `arg_list` το εξασφαλίζει, αφού τα tokens είναι δυναμικά.
2. **Κλείσιμο tab ή εφαρμογής ενώ τρέχει το curl:** η διεργασία σταματά και τα buffers ελευθερώνονται μόνο μετά το `proc_free`, χωρίς use-after-free από τα reader threads. Τεστ στο Task 2, με sanitizers.
3. **Όνομα συλλογής όπως `../../x`:** το όνομα αρχείου είναι δημιουργημένο id και ποτέ το όνομα, οπότε δεν γράφεται τίποτα εκτός του φακέλου. Τεστ στο Task 7.
4. **Secrets που επικαλύπτονται** (ένα είναι υποσύνολο άλλου): η κάλυψη εφαρμόζεται πρώτα στο μακρύτερο, ώστε να μη μένει τμήμα ορατό. Τεστ στο Task 4.
5. **Αλλαγή ή διαγραφή περιβάλλοντος ενώ είναι ανοιχτό το modal ή τρέχει αίτημα:** το τρέχον αίτημα κρατά τις τιμές με τις οποίες ξεκίνησε, και το modal υπολογίζει ξανά τι λείπει στο νέο ενεργό περιβάλλον. Τεστ στο Task 3.

---

### Task 1: JSON και κοινά αρχεία

**Files:**
- Create: `src/core/json.c`, `src/core/json.h`, `src/core/fileio.c`, `src/core/fileio.h`, `tests/test_api.c`
- Modify: `src/core/settings.c` (χρήση του `fileio`), `CMakeLists.txt`

**Interfaces:**
- Produces:
```c
typedef enum { JV_NULL, JV_BOOL, JV_NUM, JV_STR, JV_ARR, JV_OBJ } jv_type;
typedef struct jv jv;
#define JV_MAX_DEPTH 64
jv *jv_parse(const char *text, size_t len, char *err, size_t errlen); /* err: "line L column C: ..." */
void jv_free(jv *v);
jv_type jv_kind(const jv *v);          /* JV_NULL for v == NULL */
const char *jv_str(const jv *v);       /* NULL if not a string */
double jv_num(const jv *v); int jv_bool(const jv *v);
int jv_len(const jv *v); jv *jv_at(const jv *v, int i);
jv *jv_get(const jv *obj, const char *key); const char *jv_key_at(const jv *obj, int i);
jv *jv_new(jv_type t); jv *jv_new_str(const char *s); jv *jv_new_num(double n); jv *jv_new_bool(int b);
void jv_set(jv *obj, const char *key, jv *val); void jv_push(jv *arr, jv *val); /* take ownership */
char *jv_write(const jv *v, int pretty, size_t *len);                 /* caller frees */

int file_write_private(const char *path, const char *data, size_t len); /* mkdir -p, tmp + rename, 0600 */
char *file_read_all(const char *path, size_t *len, size_t max);        /* NULL if missing or too big */
int file_quarantine(const char *path, char *moved_to, size_t n);       /* path -> path.bad-N, never overwrites */
```

- [ ] **Step 1: Γράψε τα τεστ `test_jv_*`.** `jv_parse` δέχεται `{"a":[1,true,null,"é\n\u00e9"]}` και ο `jv_write` το επαναφέρει ισοδύναμο. Απορρίπτει trailing comma, δεύτερη τιμή, ανοιχτό string, βάθος 65, surrogate μόνο του, με `err` που περιέχει `line`. Το `jv_get(obj,"x")` με απόν κλειδί δίνει `NULL`. `test_file_private`: μετά το write το mode είναι `0600` (εκτός Windows), το quarantine δεν αντικαθιστά υπάρχον `.bad-1`.
- [ ] **Step 2: Τρέξε:** `ctest --test-dir build -R api --output-on-failure`. Αναμενόμενο: αποτυχία, γιατί δεν υπάρχει το `json.h`.
- [ ] **Step 3: Υλοποίησε τα `json.c` και `fileio.c`.** Το `jv_write` κρατά τη σειρά των κλειδιών, ώστε τα αρχεία να δίνουν καθαρά diffs. Μετάφερε τα `make_parents`, `open_private`, `commit_file` από το `settings.c` στο `fileio.c`, και κάνε το `settings.c` να τα χρησιμοποιεί.
- [ ] **Step 4: Τρέξε** όλο το `ctest`. Αναμενόμενο: όλα περνούν, και τα τεστ settings δεν αλλάζουν.
- [ ] **Step 5: Commit:** `core: add a JSON tree and shared private file writing`

### Task 2: Tabs

**Files:**
- Create: `src/ui/tab.c`, `src/ui/view_tabs.c`
- Modify: `src/ui/app.h`, `src/ui/app.c`, `src/ui/main.c`, `src/ui/view_center.c`, `src/ui/view_output.c`, `src/ui/view_library.c`, `src/ui/view_docs.c`, `src/ui/view_frame.c`, `CMakeLists.txt`

**Interfaces:**
- Produces:
```c
#define MAX_TABS 32
typedef struct tab tab_t;  /* req, method, cards_*, url_missing, show_recipes, method_menu, out_scroll,
                              out_follow, tab (out_tab, renamed output), pretty, proc, out, log, pretty_buf,
                              run_*, has_result, exit_code, http_status, curl_*, has_stats, pretty_*, wrap_*, args */
/* app_t: tab_t *tabs[MAX_TABS]; int ntabs, cur; */
#define CUR(a) ((a)->tabs[(a)->cur])
int tab_new(app_t *a);            /* slot, or -1 at MAX_TABS; makes it current */
void tab_close(app_t *a, int i);  /* proc_free first, then buffers; last tab leaves a fresh one */
void app_poll_all(app_t *a);
```
- Hooks: `--tab` (νέο tab· τα επόμενα `--url`, `--add`, `--run` αφορούν αυτό), `--select-tab N`, `--close-tab N`.

- [ ] **Step 1: Μετακίνησε τα πεδία στο `tab_t` και ανανέωσε κάθε χρήση σε `CUR(a)->...`,** χωρίς καμία αλλαγή συμπεριφοράς. Τα `on_out` και `on_err` παίρνουν ως `user` το `tab_t *`.
- [ ] **Step 2: Επιβεβαίωσε ότι δεν άλλαξε τίποτα:** `ctest`, και ένα `--screenshot` με `--add location --docs location` εκτός της μπάρας ίδιο με πριν. Η σύγκριση γίνεται οπτικά.
- [ ] **Step 3: Πρόσθεσε τη μπάρα tabs ύψους 32 μονάδων κάτω από το header** στο `view_tabs.c`: επιλογή, `×`, `+`, τελεία για αλλαγές (το πεδίο `dirty` το ενεργοποιεί το Task 7), τίτλος `METHOD όνομα-ή-URL` με ellipsis. Ctrl/Cmd+T, +W, +Enter.
- [ ] **Step 4: Γράψε το `tests/live/test_tabs.py`.** Με το `servers.py`, δύο tabs τρέχουν ταυτόχρονα σε αργό endpoint (`--dump` ανά tab, απόκριση σωστή στο καθένα). Κλείσιμο ενός που τρέχει (`--close-tab`) και ολόκληρης της εφαρμογής ενώ τρέχει, κάτω από sanitizers: έξοδος 0 χωρίς αναφορά ASan.
- [ ] **Step 5: Τρέξε** το τεστ και το `ctest`, και με sanitizers. Αναμενόμενο: όλα περνούν.
- [ ] **Step 6: Commit:** `ui: run each request in its own tab`

### Task 3: Μεταβλητές, πίνακας και modal

**Files:**
- Create: `src/core/vars.c`, `src/core/vars.h`, `src/core/urlquery.c`, `src/core/urlquery.h`, `src/ui/view_vars.c`, `src/ui/view_modal.c`
- Modify: `src/ui/app.c` (`app_run`), `src/ui/view_frame.c`, `src/ui/main.c`, `tests/test_api.c`

**Interfaces:**
- Consumes: `jv_*`, `file_*`, `arg_list`, `builder_build`
- Produces:
```c
typedef struct { char *name, *value; int secret; } var_t;
typedef struct { char *name; var_t *v; int n, cap; } var_env;
typedef struct { var_env *e; int n, active; } var_store;
void vars_init(var_store *s);                /* one env named "Default" */
void vars_free(var_store *s);
int vars_load(var_store *s, const char *path, char *err, size_t n); /* bad file: quarantined, defaults, err set */
int vars_save(const var_store *s, const char *path);
int vars_name_ok(const char *name);          /* [A-Za-z0-9_.-]+ */
var_t *var_find(const var_env *e, const char *name);
var_t *var_set(var_env *e, const char *name, const char *value, int secret);
int env_add(var_store *s, const char *name); int env_copy(var_store *s, int i, const char *name);
int env_rename(var_store *s, int i, const char *name);
int env_remove(var_store *s, int i);         /* refuses the last one */
int vars_suggest_secret(const char *name);   /* token secret password passwd key auth, any case */
/* Names used in args and absent from e; unique, in order of use. Caller frees with str_list_free. */
typedef struct { char **s; int n; } str_list;
int vars_missing(const var_env *e, const arg_list *tmpl, str_list *out);
void str_list_free(str_list *l);
/* real gets values, shown gets "********" for secrets. Tokens stay parallel. In the query of a URL token
   (TOK_URL or a value of --url), after the first '?', the value is percent-encoded. One pass only. */
int vars_apply(const var_env *e, const arg_list *tmpl, arg_list *real, arg_list *shown);

char *pct_encode(const char *s);             /* keeps A-Za-z0-9-_.~ */
```
- Hooks: `--var NAME=VALUE`, `--secret NAME=VALUE`, `--env NAME`, `--open-vars`, `--modal-value NAME=VALUE`, `--modal-action save|cancel|open`.

- [ ] **Step 1: Γράψε τα τεστ.**
```c
CHECK(!vars_name_ok("a b"));
/* {{"a":1}} is text, {{base}} is not, a value holding {{x}} is not re-read */
/* {{base}}/p?q={{v}} with base=https://h/api, v="a b&c" => https://h/api/p?q=a%20b%26c */
/* a 100 KB value reaches real intact (Review Focus 1) */
/* secret: real has the value, shown has ********, and the two lists have the same length */
/* missing: {{a}} {{b}} {{a}} with b defined => [a]; after switching to an env with a and not b => [b] (Review Focus 5) */
```
`vars_save` μετά `vars_load` δίνει ίδιο περιεχόμενο. Κατεστραμμένο αρχείο: quarantine, `Default`, γεμάτο `err`. Το `env_remove` αρνείται το τελευταίο.
- [ ] **Step 2: Τρέξε:** αποτυχία, γιατί δεν υπάρχει το `vars.h`.
- [ ] **Step 3: Υλοποίησε τα `vars.c` και το `pct_encode`.**
- [ ] **Step 4: Τρέξε:** περνούν.
- [ ] **Step 5: Σύνδεσε το `app_run`:** `builder_build` για το `tmpl`, `vars_missing`. Αν λείπουν, άνοιξε το modal χωρίς `proc_start` και κράτα ποιο tab περιμένει. Αλλιώς `vars_apply`, το `real` στο `proc_start`. Το Command δείχνει ό,τι βγαίνει από `shown`, και το Copy από `real`. Αν αλλάξει το ενεργό περιβάλλον όσο είναι ανοιχτό το modal, ξαναϋπολόγισε τι λείπει.
- [ ] **Step 6: Φτιάξε τον επιλογέα περιβάλλοντος στο header, το ειδικό tab Variables** (`view_vars.c`, στήλες Όνομα, Τιμή, Secret, ματάκι, διαγραφή, σημάδι για λάθος ή διπλό όνομα, και σημείωση για τιμή secret κάτω από 4 χαρακτήρες) **και το modal** (`view_modal.c`: μια γραμμή ανά μεταβλητή, Secret από `vars_suggest_secret`, Save & Run, Cancel, Open Variables). Το modal μπλοκάρει την είσοδο στα από κάτω. Αποθήκευση στο `variables.json`.
- [ ] **Step 7: Γράψε το `tests/live/test_vars.py`.** Το αίτημα δεν φτάνει στον server όσο λείπει μεταβλητή. Το Save & Run το στέλνει με την τιμή, το αρχείο την έχει, και η επόμενη εκτέλεση δεν ανοίγει modal. Το Cancel δεν στέλνει και δεν γράφει τίποτα. `env -i FOO=bar` με `{{FOO}}` ανοίγει modal, άρα το λειτουργικό δεν δίνει τιμές. Το περιβάλλον του curl δεν περιέχει τη μεταβλητή (έλεγχος με `echoargs` που τυπώνει `environ`).
- [ ] **Step 8: Τρέξε** το τεστ και το `ctest`. Αναμενόμενο: όλα περνούν.
- [ ] **Step 9: Commit:** `ui: add environments, a variables table and a prompt for missing values`

### Task 4: Κάλυψη secrets

**Files:**
- Create: `src/core/mask.c`, `src/core/mask.h`
- Modify: `src/ui/view_center.c`, `src/ui/view_output.c`, `src/ui/app.c`, `src/ui/main.c`, `tests/test_api.c`

**Interfaces:**
- Consumes: `var_env`, `arg_list`, `pct_encode`
- Produces:
```c
#define MASK_MIN_LEN 4
typedef struct mask_set mask_set;
/* From the secrets of e: exact and percent-encoded, plus base64(user:pass) for a --user value in real
   that contains a secret. Longest first. Under MASK_MIN_LEN is skipped. */
mask_set *mask_build(const var_env *e, const arg_list *real);
void mask_free(mask_set *m);
/* In place, replacing each match with as many '*' as it has bytes: length is unchanged, so wrap offsets hold. */
void mask_apply(const mask_set *m, char *buf, size_t len);
char *base64_encode(const char *s, size_t len);
```
- Hook: `--reveal` (το ματάκι του Command).

- [ ] **Step 1: Γράψε τα τεστ.** `base64_encode("user:pass")` δίνει `dXNlcjpwYXNz`. Secret `abcd` καλύπτεται παντού σε γραμμή. Το `abc` δεν καλύπτεται. Με secrets `abcd` και `abcdef`, το `xabcdefy` γίνεται `x******y` (Review Focus 4). Το `a b+c` καλύπτεται και ως `a%20b%2Bc`. Το `--user bob:{{p}}` καλύπτει και το base64 του `bob:<τιμή>`. Το μήκος δεν αλλάζει ποτέ.
- [ ] **Step 2: Τρέξε:** αποτυχία.
- [ ] **Step 3: Υλοποίησε το `mask.c`.**
- [ ] **Step 4: Τρέξε:** περνούν.
- [ ] **Step 5: Σύνδεσε.** Το tab κρατά το `mask_set` της εκτέλεσης, που φτιάχνεται στο ξεκίνημα. Το `view_output` εφαρμόζει το `mask_apply` σε αντίγραφο κάθε γραμμής πριν από το wrap και τη σχεδίαση, και στο Copy πριν από το `ui_copy`, και το ίδιο στο `--dump`. Το Command αποκτά ματάκι που γυρνά από `shown` σε `real` και μηδενίζεται στην αλλαγή tab. Το Copy με secrets δείχνει το μήνυμα «Copied with real secret values» για 3 δευτερόλεπτα.
- [ ] **Step 6: Γράψε το `tests/live/test_mask.py`.** Ο server απαντά με το token: το `--dump` δεν το περιέχει. Με Basic, το Log δεν περιέχει ούτε το base64.
- [ ] **Step 7: Τρέξε** όλα. Αναμενόμενο: περνούν.
- [ ] **Step 8: Commit:** `ui: hide secrets in the command, response and log`

### Task 5: Φόρμα Params

**Files:**
- Modify: `src/core/urlquery.c`, `src/core/urlquery.h`, `src/core/builder.h` (`form_hints`), `tests/test_api.c`
- Create: `src/ui/view_forms.c`

**Interfaces:**
- Consumes: `pct_encode`
- Produces:
```c
typedef struct { char *key, *value; int enabled; } kv_row;
typedef struct { kv_row *r; int n, cap; } kv_list;
void kv_free(kv_list *l); kv_row *kv_add(kv_list *l, const char *k, const char *v, int enabled);
typedef struct { char auth[16]; kv_list disabled_params, disabled_headers; } form_hints;
char *pct_decode(const char *s, int plus_is_space);  /* a bad % stays */
int query_parse(const char *url, kv_list *rows);     /* decoded; {{...}} kept */
char *query_build(const char *url, const kv_list *rows); /* encodes enabled rows, keeps {{...}} and #fragment */
```
- Hook: `--form params|headers|body|auth|curl`.

- [ ] **Step 1: Γράψε τα τεστ.**
```c
/* key "q", value "καφές & γάλα" => ?q=%CE%BA%CE%B1%CF%86%CE%AD%CF%82%20%26%20%CE%B3%CE%AC%CE%BB%CE%B1 */
/* "a=b" as a value => a%3Db, "+" read from a URL => space, "%zz" stays */
/* parse then build of ?a=x%20y&b=%7B gives the same text: no double encoding */
/* value {{v}} stays {{v}}; https://h/p?a=1#top keeps #top last; a disabled row leaves the URL and stays in rows */
```
- [ ] **Step 2: Τρέξε:** αποτυχία.
- [ ] **Step 3: Υλοποίησε τα `pct_decode`, `query_parse`, `query_build`.**
- [ ] **Step 4: Τρέξε:** περνούν.
- [ ] **Step 5: Φτιάξε στο `view_forms.c` τις καρτέλες Params, Headers, Body, Auth, Curl,** με το Curl να δείχνει τις σημερινές κάρτες και το Params τον πίνακα. Αλλαγή πίνακα ξαναγράφει το URL. Αλλαγή του URL ξαναγράφει τον πίνακα, χωρίς να πειράζει το κείμενο της μπάρας.
- [ ] **Step 6: Γράψε το `tests/live/test_params.py`:** ο server λαμβάνει αποκωδικοποιημένο `q=καφές & γάλα`.
- [ ] **Step 7: Commit:** `ui: edit URL parameters as a table, URL encoded`

### Task 6: Φόρμες Headers, Body, Auth

**Files:**
- Create: `src/core/forms.c`, `src/core/forms.h`
- Modify: `src/ui/view_forms.c`, `tests/test_api.c`

**Interfaces:**
- Consumes: `request`, `kv_list`, `form_hints`, `pct_encode`
- Produces:
```c
typedef enum { BODY_NONE, BODY_RAW, BODY_JSON, BODY_FORMDATA, BODY_URLENC, BODY_FILE, BODY_OPTIONS } body_kind;
typedef enum { AUTH_NONE, AUTH_BASIC, AUTH_BEARER, AUTH_DIGEST, AUTH_APIKEY, AUTH_OPTIONS } auth_kind;
void form_headers_get(const request *r, const form_hints *h, kv_list *rows);
void form_headers_set(request *r, form_hints *h, const kv_list *rows);
body_kind form_body_kind(const request *r);
int form_body_get(const request *r, kv_list *rows, char **text); /* rows or text, by kind */
int form_body_set(request *r, body_kind k, const kv_list *rows, const char *text, char *err, size_t n);
auth_kind form_auth_kind(const request *r, const form_hints *h);
/* name:value for basic/digest (user, pass), bearer (token), apikey (name, value, "header"|"query") */
int form_auth_set(request *r, form_hints *h, auth_kind k, const char *a, const char *b, const char *where,
                  char *err, size_t n);
int form_auth_literal_secret(const request *r);      /* a password or token not written as {{...}} */
```

- [ ] **Step 1: Γράψε τα τεστ.** Πίνακας από spec §6, ένα ανά τύπο:
  - raw `@x` δίνει `--data-raw @x`.
  - JSON δίνει `--json`.
  - form-data με κείμενο `@y` δίνει `--form-string a=@y`, και με αρχείο `--form f=@/p`.
  - urlencoded με όνομα `a b` δίνει `--data-urlencode a%20b=c d`.
  - file δίνει `--data-binary @/p`.
  - Basic `bob` / `p:w` δίνει `--basic --user bob:p:w`.
  - Basic με username `a:b` δίνει σφάλμα που περιέχει `username`.
  - Bearer δίνει `--oauth2-bearer t`.
  - `--data` μαζί με `--form` δίνει `BODY_OPTIONS`, και το `form_body_set` δεν σβήνει τίποτα.
  - Αλλαγή από JSON σε raw αφαιρεί το `--json`, και το `--digest` μαζί με `--basic` δίνει `AUTH_OPTIONS`.
- [ ] **Step 2: Τρέξε:** αποτυχία.
- [ ] **Step 3: Υλοποίησε το `forms.c`.**
- [ ] **Step 4: Τρέξε:** περνούν.
- [ ] **Step 5: Φτιάξε τις όψεις.** Κάθε γραμμή στέλνει hover docs στην αντίστοιχη επιλογή. Κατάσταση `*_OPTIONS`: «Set by options» με κουμπί προς το Curl. Προειδοποίηση για μη `https://` στα Basic, Digest, Bearer, API key, και για `form_auth_literal_secret`.
- [ ] **Step 6: Γράψε το `tests/live/test_forms.py`** με πραγματικό curl και τοπικό server. Το header που φτάνει ισούται με `"Basic " + base64("bob:p:w")`, και το ίδιο με username `βασίλης` σε UTF-8. Κάθε τύπος Body φτάνει όπως πρέπει.
- [ ] **Step 7: Commit:** `ui: add header, body and auth forms over curl's options`

### Task 7: Αριστερό πανέλ, συλλογές, session

**Files:**
- Create: `src/core/reqjson.c`, `src/core/reqjson.h`, `src/core/collection.c`, `src/core/collection.h`, `src/ui/view_collections.c`
- Modify: `src/ui/view_frame.c`, `src/ui/main.c`, `src/ui/view_settings.c`, `src/core/settings.c`, `src/core/settings.h`, `tests/test_api.c`

**Interfaces:**
- Consumes: `jv_*`, `file_*`, `request`, `form_hints`
- Produces:
```c
/* extra: options this curl UI does not know, kept verbatim */
jv *req_to_jv(const request *r, const char *method, const form_hints *h, const jv *extra);
int req_from_jv(const jv *v, request *r, char *method, size_t n, form_hints *h, jv **extra);

typedef enum { NODE_FOLDER, NODE_REQUEST } node_kind;
typedef struct coll_node { node_kind kind; char *name; char id[17]; jv *req;
                           struct coll_node **kids; int nkids; struct coll_node *parent; } coll_node;
typedef struct { char id[17]; coll_node *root; int read_only; } collection;
collection *coll_new(const char *name);      /* id: 16 random hex digits */
int coll_load_dir(const char *dir, collection ***out, int *n, char *err, size_t errn);
int coll_save(const collection *c, const char *dir);  /* <dir>/<id>.json; refuses read_only */
coll_node *coll_add(coll_node *parent, node_kind k, const char *name, jv *req);
void coll_remove(coll_node *n); coll_node *coll_copy(const coll_node *n);
int coll_move(coll_node *n, coll_node *new_parent);   /* refuses moving into its own subtree */
void coll_free(collection *c);
```
Το session γράφεται από το `src/ui/app.c`, με `req_to_jv` ανά tab, στο `session.json`: `{ "tabs":[{"req":...,"node":id|null,"dirty":bool}], "cur":n }`.
- Hooks: `--sidebar collections|history|options`, `--save-as COLLECTION/NAME`, `--open-node ID`.

- [ ] **Step 1: Γράψε τα τεστ.** Round trip αιτήματος με επιλογές, `negated`, κενές και ελληνικές τιμές. Άγνωστη επιλογή `--future` μένει μετά από φόρτωση και αποθήκευση. Συλλογή με όνομα `../../x` γράφει μόνο `<dir>/<16 hex>.json` (Review Focus 3). `version: 2` δίνει `read_only` και το `coll_save` αποτυγχάνει. Κατεστραμμένο αρχείο πηγαίνει σε `.bad-1` και οι υπόλοιπες συλλογές φορτώνονται. Το `coll_move` σε απόγονο αρνείται.
- [ ] **Step 2: Τρέξε:** αποτυχία.
- [ ] **Step 3: Υλοποίησε τα `reqjson.c`, `collection.c`.** Μορφή όπως στο spec §9, με `format` και `version`.
- [ ] **Step 4: Τρέξε:** περνούν.
- [ ] **Step 5: Φτιάξε το πανέλ.** Τρεις όψεις με το `view_library` ως Options, δέντρο με αναζήτηση, μενού για μετονομασία, αντιγραφή, μετακίνηση, διαγραφή με επιβεβαίωση. Ctrl/Cmd+S (νέο αίτημα: modal με όνομα και θέση). `dirty` με σύγκριση του `req_to_jv` με το αποθηκευμένο. Κλείσιμο με αλλαγές: Save, Discard, Cancel. Κλικ σε ήδη ανοιχτό αίτημα πηγαίνει στο tab του.
- [ ] **Step 6: Φτιάξε τα session και `reopen_tabs`** (προεπιλογή 1, αντικαθιστά τα `remember_request` και την αντίστοιχη σειρά στο Settings). Off: σβήνει το `session.json` στο κλείσιμο και ρωτά για tabs με αλλαγές. Μετατροπή: `last-request.txt` με `remember_request=1` ανοίγει μία φορά ως tab και μετά σβήνεται.
- [ ] **Step 7: Γράψε το `tests/live/test_store.py`.** Αποθήκευση, επανεκκίνηση και ίδια tabs. Και το τεστ του spec §12: μοναδική τιμή secret περνά από εκτέλεση, αποθήκευση, ιστορικό και session, και μετά από σάρωση όλων των αρχείων του φακέλου ρυθμίσεων, εμφανίζεται μόνο στο `variables.json`.
- [ ] **Step 8: Τρέξε** όλα. Αναμενόμενο: περνούν.
- [ ] **Step 9: Commit:** `ui: save requests in collections and reopen tabs`

### Task 8: Ιστορικό

**Files:**
- Create: `src/core/history.c`, `src/core/history.h`, `src/ui/view_history.c`
- Modify: `src/ui/app.c`, `tests/test_api.c`

**Interfaces:**
- Consumes: `req_to_jv`, `req_from_jv`, `file_*`
- Produces:
```c
#define HISTORY_MAX 500
typedef struct { long long when; int status; double secs; long long size; char *method, *url; jv *req; } hist_entry;
typedef struct { hist_entry *e; int n; } history;
int history_load(history *h, const char *path);   /* bad lines skipped, newest HISTORY_MAX kept */
int history_append(history *h, const char *path, hist_entry *take); /* appends; rewrites over HISTORY_MAX + 100 lines */
int history_clear(history *h, const char *path);
void history_free(history *h);
```

- [ ] **Step 1: Γράψε τα τεστ.** 700 προσθήκες αφήνουν 500, τις νεότερες, στη μνήμη και μετά από φόρτωση. Σπασμένη γραμμή παραλείπεται χωρίς να χαθούν οι άλλες. Η εγγραφή κρατά `{{token}}`.
- [ ] **Step 2: Τρέξε:** αποτυχία.
- [ ] **Step 3: Υλοποίησε το `history.c`.**
- [ ] **Step 4: Τρέξε:** περνούν.
- [ ] **Step 5: Σύνδεσε:** εγγραφή όταν τελειώνει μια εκτέλεση, με το αίτημα πριν από την αντικατάσταση. Όψη με ώρα, method, URL, status, χρόνο, μέγεθος. Κλικ ανοίγει νέο tab. Clear history με επιβεβαίωση.
- [ ] **Step 6: Commit:** `ui: keep a history of runs`

### Task 9: Εισαγωγή εντολής curl

**Files:**
- Create: `src/core/cmdparse.c`, `src/core/cmdparse.h`
- Modify: `src/ui/view_modal.c`, `src/ui/view_tabs.c`, `src/ui/main.c`, `tests/test_api.c`

**Interfaces:**
- Consumes: `request`, `options_find`, `g_options`
- Produces:
```c
/* 0 and a filled request, or -1 with err naming the problem and r unchanged. */
int cmd_parse(const char *text, request *r, char *method, size_t methodn, char *err, size_t errn);
```
- Hook: `--import-cmd TEXT`.

- [ ] **Step 1: Γράψε τα τεστ.**
  - Δέχεται: `curl -sSL https://a` (τρεις επιλογές και URL), `-XPOST`, `-d@f`, `--no-location`, `"a b"`, `'a"b'`, `a\ b`, συνέχεια με `\` και αλλαγή γραμμής, χωρίς αρχικό `curl`, `$` μέσα σε μονά εισαγωγικά, `"C:\x"`.
  - Απορρίπτει, με το όνομα στο `err`: `--nope`, `-H` χωρίς τιμή, ανοιχτό εισαγωγικό, `--next`, δύο URL, `$HOME`, `$(x)`, backtick, `|`, `;`, `&`, `<`, `>` εκτός εισαγωγικών, `^` σε τέλος γραμμής.
  - Σε απόρριψη το `r` μένει άδειο.
  - Για κάθε όνομα του `g_options`, μια εντολή με αυτό δίνει το σωστό `opt`.
- [ ] **Step 2: Τρέξε:** αποτυχία.
- [ ] **Step 3: Υλοποίησε το `cmdparse.c`,** με τους κανόνες τύπου επιλογής από τον πίνακα του `options_data`.
- [ ] **Step 4: Τρέξε:** περνούν.
- [ ] **Step 5: Φτιάξε το modal** με επικόλληση, σύνδεσμο δίπλα στο `+` της μπάρας tabs, σφάλμα μέσα στο modal, και νέο tab στην επιτυχία.
- [ ] **Step 6: Commit:** `ui: import a pasted curl command`

### Task 10: README και έκδοση

**Files:**
- Modify: `README.md`, `CMakeLists.txt`, `docs/screenshots/*`

- [ ] **Step 1: Ανέβασε την έκδοση σε `0.4.0`.** Τρέξε `ctest`, sanitizers, όλα τα `tests/live/test_*.py`, και το πλήρες `tests/live/live_options.py --ui build/curl-ui --report results.md`. Αναμενόμενο: καμία νέα αποτυχία σε σχέση με τα 182 effect, 90 ran, 12 n/a.
- [ ] **Step 2: Ξαναπάρε τα screenshots** με νέα: Variables, modal, φόρμα Auth, συλλογές, ιστορικό. Χρησιμοποίησε endpoint που δεν επιστρέφει τη δημόσια IP και έλεγξε κάθε εικόνα για προσωπικά στοιχεία.
- [ ] **Step 3: Γράψε το README:** νέες λειτουργίες, Settings με `reopen_tabs`, αρχεία, hooks, τα όρια του spec §10. Αφαίρεσε το «no saved collections» από το «When something else fits better». Σύγκρινε κάθε αναφορά με την εφαρμογή.
- [ ] **Step 4: Commit:** `Release 0.4.0 notes, version and screenshots`
- [ ] **Step 5: Ζήτα από τον χρήστη να δοκιμάσει με το χέρι και να εγκρίνει το tag `v0.4.0`.** Μην κόψεις tag πριν από αυτό.
