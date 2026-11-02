# OpenAPI Import Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Import a Swagger or OpenAPI file of any published version as a collection that runs at once.

**Architecture:** Four SDL-free stages in `src/core`: YAML or JSON to a `jv` tree; a loader that produces one self-contained tree with every `$ref` inlined; one reader per family filling a small model; and the model to a `collection` and its `var_store`. The app only picks the file, calls this, and shows the report.

**Tech Stack:** C11, no new dependencies, ctest, Python live tests.

**Spec:** `docs/superpowers/specs/2026-11-02-openapi-import-design.md`

## Global Constraints

- C11 only; no new library. No shell runs curl. The app opens no network connection.
- Branch `api-client`. No merge or tag without the user's word.
- No Co-Authored-By or Claude-Session line in any commit.
- CI runs all tests in the `common` job: add each new ctest and live test there only.
- Before writing against a specification, read that version through context7 (`/websites/spec_openapis_oas_v3_2_0`, `/websites/spec_openapis_oas_v3_0_3`, `/oai/openapi-specification` at 3.1.1, `/websites/swagger_io_specification`). Swagger 1.x: the 1.2 document in the OAI repository.
- Limits, verbatim: 256 files and 64 MB in all; YAML nesting 128; schema generation depth 12.
- `#ifdef`-free in `src/core` except where `fsdir` already hides it: paths through `fs_*`, files through `file_read_all`.
- Check test results before each commit and push; never push on a failed run.

## Review Focus

1. A real, large spec with long descriptions, block scalars and comments: imports, or refuses with a line number; never a crash. Test in Task 1 (`yaml_real_shapes`), Task 7 (Pet Store).
2. A truncated or damaged file: an error, nothing created. Test in Task 7 (`damaged_every_64th_byte`).
3. A hostile file: alias bomb, `$ref` cycle, a `$ref` leaving the folder. Limits hold, and the report names what left. Tests in Task 1 and Task 2.
4. Names from the file that are not valid variable names (`X-Api-Key`, `pet id`): the request still works. `vars_name_ok` allows `[A-Za-z0-9_.-]+`, anything else becomes `_`. Test in Task 6 (`names_made_valid`).
5. An operation with no server, no tag or no name: still a runnable request, named `METHOD /path`. Test in Task 6 (`bare_operation`).

---

### Task 1: YAML reader

**Files:**
- Create: `src/core/yaml.c`, `src/core/yaml.h`, `tests/test_yaml.c`
- Modify: `CMakeLists.txt`, `src/core/json.h` only if a helper is missing

**Interfaces:**
- Produces: `jv *yaml_parse(const char *text, size_t len, char *err, size_t errn);` returns NULL with `line L column C: reason` in err. Input starting with `{` or `[` after whitespace goes to `jv_parse`.

- [ ] **Step 1: Write the failing tests** in `tests/test_yaml.c`, each comparing `jv_write` of the result with the JSON text:
  - `yaml_maps_and_lists`, `yaml_flow`, `yaml_scalars`: `true`, `null`, `~`, `1`, `1.5`, `"1"`, `'it''s'`, `"é\n"`; a version such as `3.0` stays a string only when quoted.
  - `yaml_block_scalars`: `|`, `|-`, `|+`, `>`, `>-`.
  - `yaml_multiline_plain`, `yaml_comments` (`a: b # c` gives `b`; `a: b#c` gives `b#c`).
  - `yaml_anchor_alias_merge`: `&a`, `*a`, `<<: *a`, with own keys winning.
  - `yaml_real_shapes`: list items at the indent of their key; `- name: x` with following keys; keys such as `'200':`, `/pets/{id}:`, `application/json:`; a `---` first line; `x: []`, `x: {}`.
  - Refusals with line numbers: `yaml_refuses_tab_indent`, `yaml_refuses_two_documents`, `yaml_refuses_unknown_tag`, `yaml_refuses_bad_indent`, `yaml_refuses_duplicate_alias_target` (undefined alias).
  - Limits: `yaml_nesting_limit` (129 levels refused), `yaml_alias_bomb` (ten levels of nine aliases refused, returning in under a second).

- [ ] **Step 2: Add `test_yaml` as ctest `yaml`, run it.** Run: `ctest --test-dir $B -R yaml --output-on-failure`. Expected: FAIL.
- [ ] **Step 3: Implement** as a recursive-descent reader over lines with indents. Aliases are `jv_clone`d, counted against a budget of 1,000,000 nodes.
- [ ] **Step 4: Run under ASan as well.** Expected: PASS on both.
- [ ] **Step 5: Commit** `core: a YAML reader for API descriptions`.

### Task 2: Loader and `$ref`

**Files:**
- Create: `src/core/oas_load.c`, `src/core/oas_load.h`, `tests/test_openapi.c`, `tests/fixtures/openapi/refs/*`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `yaml_parse`, `file_read_all`, `fs_parent`, `fs_join`, `fs_canon`.
- Produces:
```c
typedef struct { int files, remote_refs, broken_refs, cycles; str_list outside; } oas_load_report;
jv *oas_load(const char *path, oas_load_report *rep, char *err, size_t errn);
void oas_load_report_free(oas_load_report *r);
```
The result has every local `$ref` inlined. A `$ref` beside siblings (3.1) merges them over the target. Swagger 1.x: a listing whose `apis` have no `operations` reads each as `<folder>/<path>` then `<path>.json`, under the added key `_declarations`.

- [ ] **Step 1: Write the failing tests** with fixture files: `ref_same_file`, `ref_other_file_relative`, `ref_json_pointer_escapes` (`~1`, `~0`, `%7B`), `ref_siblings_merge`, `ref_cycle_stops` (`cycles == 1`, target replaced by `{}`), `ref_remote_counted` (left out, `remote_refs == 1`), `ref_broken_counted`, `ref_outside_folder_listed` (loaded and its canonical path in `outside`), `load_limit_files` (257 files refused), `load_limit_bytes`, `swagger12_folder`.
- [ ] **Step 2: Run.** Run: `ctest --test-dir $B -R openapi --output-on-failure`. Expected: FAIL.
- [ ] **Step 3: Implement.** Files cached by canonical path. Inlining budget 2,000,000 nodes, past which the load is refused with the reason.
- [ ] **Step 4: Run under ASan.** Expected: PASS.
- [ ] **Step 5: Commit** `core: load an API description, with its references inlined`.

### Task 3: The model and OpenAPI 3.x

**Files:**
- Create: `src/core/oas_model.h`, `src/core/oas_model.c`, `src/core/oas_read3.c`, `tests/fixtures/openapi/v3.0.0.yaml`, `v3.0.4.json`, `v3.1.0.yaml`, `v3.1.2.json`, `v3.2.0.yaml`, `v3.2.1.json`
- Modify: `CMakeLists.txt`, `tests/test_openapi.c`

**Interfaces:**
- Produces:
```c
typedef enum { OAS_BODY_NONE, OAS_BODY_JSON, OAS_BODY_URLENC, OAS_BODY_FORM, OAS_BODY_RAW, OAS_BODY_FILE } oas_body_kind;
typedef enum { OAS_AUTH_NONE, OAS_AUTH_BASIC, OAS_AUTH_BEARER, OAS_AUTH_DIGEST, OAS_AUTH_KEY_HEADER,
               OAS_AUTH_KEY_QUERY, OAS_AUTH_KEY_COOKIE, OAS_AUTH_OAUTH, OAS_AUTH_MTLS } oas_auth_kind;
typedef struct { char *name, *value; int required, is_file; } oas_field;  /* value NULL: use {{name}} */
typedef struct { oas_field *f; int n; } oas_fields;
typedef struct {
  char method[16]; char *path, *name; char **folders; int nfolders;  /* outermost first */
  oas_fields query, headers, cookies, body_rows;
  char *raw_query;              /* in: querystring */
  oas_body_kind body; char *body_text, *content_type;
  oas_auth_kind auth; char *auth_name;
} oas_op;
typedef struct { char *name, *url; oas_fields vars; } oas_server;
typedef struct {
  char *title; oas_server *servers; int nservers; oas_op *ops; int nops;
  int webhooks, callbacks, oauth, alternatives_dropped, xml_bodies;
} oas_api;
void oas_api_free(oas_api *a);
char *oas_sample(const jv *schema);          /* the spec's order; malloc'd JSON, depth limit 12 */
int oas_read(const jv *root, oas_api *out, char *err, size_t errn);  /* dispatches by version */
int oas_read3(const jv *root, oas_api *out, char *err, size_t errn);
```

- [ ] **Step 1: Read 3.0.3, 3.1.1 and 3.2.0 through context7**, for Path Item, Operation, Parameter, Request Body, Security, Server, Tag.
- [ ] **Step 2: Write the fixtures and failing tests.** All six fixtures describe the same small API: `GET /pets` (tag `pets`, required `limit` with example `10`, optional `tag`, optional header `X-Trace`), `POST /pets` (JSON, schema with `example`), `GET /pets/{petId}`, `POST /upload` (multipart with `format: binary`), `POST /login` (urlencoded), `PUT /raw` (`text/plain`), `PUT /bin` (`application/octet-stream`), and a security scheme for each kind in the mapping table. `same_result_each_3x`: `oas_read` of all six gives the same model. Then one test per row of the spec's mapping table, including: `accept_header_param_ignored`, `path_level_params_merge` (operation wins on `name`+`in`), `security_operation_over_top`, `security_empty_array_means_none`, `first_alternative_counted`.
  Version tests: `t31_type_array_and_const`, `t31_examples_array`, `t32_query_method`, `t32_additional_operations` (`COPY`), `t32_tag_parent_nests_folders` (spec example `partner` under `external`), `t32_querystring`. Sample tests: `sample_order`, `sample_allof_merge`, `sample_oneof_first`, `sample_depth_limit`.
- [ ] **Step 3: Run.** Expected: FAIL.
- [ ] **Step 4: Implement** `oas_model.c` and `oas_read3.c`. Operation methods: `get put post delete options head patch trace query`, then `additionalOperations`.
- [ ] **Step 5: Run under ASan.** Expected: PASS.
- [ ] **Step 6: Commit** `core: read OpenAPI 3.0, 3.1 and 3.2`.

### Task 4: Swagger 2.0

**Files:**
- Create: `src/core/oas_read2.c`, `tests/fixtures/openapi/v2.0.yaml`, `v2.0.json`
- Modify: `src/core/oas_model.h`, `src/core/oas_model.c`, `tests/test_openapi.c`

**Interfaces:**
- Consumes: the model of Task 3.
- Produces: `int oas_read2(const jv *root, oas_api *out, char *err, size_t errn);`, reached through `oas_read`.

- [ ] **Step 1: Read the 2.0 specification through context7 or the OAI repository.**
- [ ] **Step 2: Write the fixtures (the same API) and failing tests:** `same_result_as_3x` (except what 2.0 cannot say, listed in the test), `s2_host_basepath_schemes` (one server per scheme), `s2_no_host_empty_base`, `s2_body_param`, `s2_formdata_file`, `s2_consumes_operation_over_top`, `s2_security_definitions`.
- [ ] **Step 3: Run.** Expected: FAIL.
- [ ] **Step 4: Implement.** **Step 5: Run under ASan.** Expected: PASS.
- [ ] **Step 6: Commit** `core: read Swagger 2.0`.

### Task 5: Swagger 1.x

**Files:**
- Create: `src/core/oas_read1.c`, `tests/fixtures/openapi/v1.2/`, `v1.1/`, `v1.0/`
- Modify: `src/core/oas_model.h`, `src/core/oas_model.c`, `tests/test_openapi.c`

**Interfaces:**
- Produces: `int oas_read1(const jv *root, oas_api *out, char *err, size_t errn);`, reached through `oas_read`.

- [ ] **Step 1: Read the 1.2 document in the OAI repository.**
- [ ] **Step 2: Write the fixtures and failing tests:** `s12_folder`, `s12_paramtype_each`, `s12_authorizations`, `s11_httpmethod_and_nickname` (`httpMethod` for `method`), `s10_same_shape`, `s1x_other_shape_refused` (the message names what was found), `s1x_resource_folder_per_declaration`.
- [ ] **Step 3: Run.** Expected: FAIL.
- [ ] **Step 4: Implement.** **Step 5: Run under ASan.** Expected: PASS.
- [ ] **Step 6: Commit** `core: read Swagger 1.x`.

### Task 6: Model to collection

**Files:**
- Create: `src/core/oas_import.c`, `src/core/oas_import.h`
- Modify: `CMakeLists.txt`, `tests/test_openapi.c`

**Interfaces:**
- Consumes: `oas_load`, `oas_read`, `coll_new`, `coll_add`, `req_to_jv`, `form_headers_set`, `form_body_set`, `form_auth_set`, `query_build`, `env_add`, `var_set`, `vars_suggest_secret`, `vars_init`.
- Produces:
```c
typedef struct { int requests; oas_load_report load; int webhooks, callbacks, oauth, alternatives_dropped, xml_bodies; } oas_report;
/* New objects; the caller adds them to the app. taken_names: for "Name 2". */
int oas_import_file(const char *path, const char *const *taken_names, int ntaken,
                    collection **out, var_store *vars, oas_report *rep, char *err, size_t errn);
void oas_report_text(const oas_report *r, const char *coll_name, char *out, size_t n);
int oas_looks_like_api(const char *text, size_t len);  /* a top-level openapi or swagger, or swaggerVersion, key */
```

- [ ] **Step 1: Write the failing tests**, reading requests back with `req_from_jv` and comparing the command from `builder`: `collection_name_taken`, `folders_by_tag`, `url_has_baseurl_and_path_vars`, `params_on_and_off`, `environments_per_server`, `server_vars_in_env`, `no_server_baseurl_empty`, `secrets_empty_and_secret`, `default_values_stay_plain`, `each_body_kind`, `each_auth_kind`, `cookie_param`, `custom_method`, `names_made_valid`, `bare_operation`, `report_text` (exact: `Imported 42 requests into Pet Store. Skipped: 2 webhooks, 1 remote $ref.`), `report_outside_files`, `looks_like_api_json_yaml_and_not_curl`.
- [ ] **Step 2: Run.** Expected: FAIL.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run under ASan.** Expected: PASS.
- [ ] **Step 5: Commit** `core: an API description becomes a collection and its variables`.

### Task 7: In the app, end to end

**Files:**
- Create: `tests/live/test_openapi.py`
- Modify: `src/ui/store.c`, `src/ui/app.h`, `src/ui/view_picker.c`, `src/ui/view_collections.c`, `src/ui/main.c`, `.github/workflows/curl-ui.yml`, `README.md`

**Interfaces:**
- Consumes: Task 6.
- Produces: `int app_import_openapi(app_t *a, const char *path, char *err, size_t errn);` and the hook `--import-openapi FILE`.

- [ ] **Step 1: Write `tests/live/test_openapi.py`:** for each fixture, import with the hook, set `baseUrl` to a local server, open each request, Run, and assert method, path, query, headers, auth and body received. Also: `import_runs_nothing`, `dialog_open_file_imports` (through the picker), `curl_file_still_goes_to_box`, `damaged_every_64th_byte` (each cut fixture exits 0 with an error or a collection, under ASan), `nothing_created_on_refusal`, `report_in_notice`, and `petstore_official` (fetches the official examples; prints skip when offline; Linux only).
- [ ] **Step 2: Run.** Expected: FAIL.
- [ ] **Step 3: Implement.** In `deliver` for `PICK_FOR_IMPORT`: `oas_looks_like_api` decides between `app_import_openapi` (closing the dialog) and the box. `app_import_openapi` saves the collection, stores its `var_store` under its id through `book_get`, calls `app_vars_save`, opens the collection in the tree and shows `oas_report_text` with `app_notice`. Filter in the picker: none by default, as the same dialog also takes a command.
- [ ] **Step 4: Run all tests:** ctest, all of `tests/live/test_*.py`, under ASan too. Expected: all pass.
- [ ] **Step 5: Add the test to the `common` job; update the README** (Using it, Collections, Known limitations with the spec's limits, Command line, Layout) and add a screenshot.
- [ ] **Step 6: Commit and push; run CI.** Run: `gh workflow run curl-ui.yml --repo VGeroutskis/curl-ui --ref api-client`. Expected: all jobs succeed.
