/* API descriptions: loading, each version, and what they become. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fileio.h"
#include "fsdir.h"
#include "oas_load.h"
#include "oas_model.h"
#include "yaml.h"

static int g_fail, g_checks;
static const char *g_fix, *g_scratch;

#define CHECK(c)                                          \
  do {                                                    \
    g_checks++;                                           \
    if(!(c)) {                                            \
      g_fail++;                                           \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
    }                                                     \
  } while(0)
#define CHECK_STR(a, b)                                                     \
  do {                                                                      \
    const char *a_ = (a), *b_ = (b);                                        \
    g_checks++;                                                             \
    if(!a_ || strcmp(a_, b_)) {                                             \
      g_fail++;                                                             \
      printf("FAIL %s:%d:\n  got  [%s]\n  want [%s]\n", __FILE__, __LINE__, \
             a_ ? a_ : "(null)", b_);                                       \
    }                                                                       \
  } while(0)

static char *fix(const char *name)
{
  return fs_join(g_fix, name);
}

/* Loads a fixture, and compares what comes out with the JSON text. */
static void loads(int line, const char *name, const char *json, oas_load_report *rep)
{
  char *path = fix(name), err[512] = "";
  jv *got = oas_load(path, rep, err, sizeof(err));
  jv *want = jv_parse(json, strlen(json), NULL, 0);
  char *a = got ? jv_write(got, 0, NULL) : NULL, *b = jv_write(want, 0, NULL);
  g_checks++;
  if(!a || strcmp(a, b)) {
    g_fail++;
    printf("FAIL line %d: %s: %s\n  got  %s\n  want %s\n", line, name, err, a ? a : "(null)", b);
  }
  free(a);
  free(b);
  free(path);
  jv_free(got);
  jv_free(want);
}

static void ref_same_file(void)
{
  oas_load_report r;
  loads(__LINE__, "refs/same.yaml",
        "{\"a\":{\"v\":1},\"b\":{\"v\":2},\"c\":{\"v\":3},\"d\":{\"v\":4},\"e\":{\"v\":5},"
        "\"defs\":{\"x\":{\"v\":1},\"a/b\":{\"v\":2},\"c~d\":{\"v\":3},\"{id}\":{\"v\":4}},\"list\":[\"zero\",{\"v\":5}]}", &r);
  CHECK(r.files == 1 && !r.broken_refs && !r.remote_refs && !r.cycles);
  oas_load_report_free(&r);
}

static void ref_other_file_relative(void)
{
  oas_load_report r;
  loads(__LINE__, "refs/other.yaml", "{\"p\":{\"n\":3}}", &r);
  CHECK(r.files == 3);
  CHECK(r.outside.n == 0);
  oas_load_report_free(&r);
}

static void ref_siblings_merge(void)
{
  oas_load_report r;
  loads(__LINE__, "refs/siblings.yaml", "{\"a\":{\"description\":\"mine\",\"x\":1},\"d\":{\"description\":\"orig\",\"x\":1}}", &r);
  oas_load_report_free(&r);
}

static void ref_cycle_stops(void)
{
  oas_load_report r;
  char *path = fix("refs/cycle.yaml"), err[256];
  jv *v = oas_load(path, &r, err, sizeof(err));
  CHECK(v != NULL);
  CHECK(r.cycles >= 1);
  CHECK(jv_len(jv_get(jv_get(v, "r"), "next")) == 0);
  CHECK(jv_kind(jv_get(jv_get(v, "r"), "next")) == JV_OBJ);
  jv_free(v);
  free(path);
  oas_load_report_free(&r);
}

static void ref_remote_counted(void)
{
  oas_load_report r;
  loads(__LINE__, "refs/remote.yaml", "{\"a\":{}}", &r);
  CHECK(r.remote_refs == 1);
  oas_load_report_free(&r);
}

static void ref_broken_counted(void)
{
  oas_load_report r;
  loads(__LINE__, "refs/broken.yaml", "{\"a\":{},\"b\":{}}", &r);
  CHECK(r.broken_refs == 2);
  oas_load_report_free(&r);
}

static void ref_outside_folder_listed(void)
{
  oas_load_report r;
  char *out = fix("refs/out.yaml"), *canon = fs_canon(out);
  loads(__LINE__, "refs/in/main.yaml", "{\"a\":{\"v\":1}}", &r);
  CHECK(r.outside.n == 1);
  if(r.outside.n == 1)
    CHECK_STR(r.outside.s[0], canon);
  free(out);
  free(canon);
  oas_load_report_free(&r);
}

static void load_limit_files(void)
{
  char *dir = fs_join(g_scratch, "many"), name[64], body[128], err[256] = "";
  int i;
  oas_load_report r;
  char *first = NULL;
  jv *v;
  for(i = 0; i <= OAS_MAX_FILES; i++) {
    char *p;
    snprintf(name, sizeof(name), "f%d.yaml", i);
    p = fs_join(dir, name);
    snprintf(body, sizeof(body), "a: {$ref: 'f%d.yaml'}\n", i + 1);
    CHECK(!file_write_private(p, body, strlen(body)));
    if(!i)
      first = p;
    else
      free(p);
  }
  v = oas_load(first, &r, err, sizeof(err));
  CHECK(v == NULL);
  CHECK(strstr(err, "256") != NULL);
  jv_free(v);
  free(first);
  free(dir);
  oas_load_report_free(&r);
}

static void load_limit_bytes(void)
{
  char *p = fs_join(g_scratch, "big.yaml"), err[256] = "";
  size_t n = OAS_MAX_BYTES + 16;
  char *body = malloc(n);
  oas_load_report r;
  jv *v;
  memset(body, ' ', n);
  body[n - 1] = '\n';
  CHECK(!file_write_private(p, body, n));
  v = oas_load(p, &r, err, sizeof(err));
  CHECK(v == NULL);
  CHECK(strstr(err, "64 MB") != NULL);
  jv_free(v);
  free(body);
  free(p);
  oas_load_report_free(&r);
}

static void load_refusals(void)
{
  oas_load_report r;
  char *p = fix("refs/none.yaml"), err[256] = "";
  jv *v = oas_load(p, &r, err, sizeof(err));
  CHECK(v == NULL && err[0]);
  jv_free(v);
  free(p);
  oas_load_report_free(&r);
}

static void swagger12_folder(void)
{
  oas_load_report r;
  char *p = fix("refs/s12/api-docs.json"), err[256] = "";
  jv *v = oas_load(p, &r, err, sizeof(err));
  jv *d = jv_get(v, "_declarations");
  CHECK(v != NULL);
  CHECK(jv_len(d) == 2);
  CHECK_STR(jv_str(jv_get(jv_at(d, 0), "resourcePath")), "/pets");
  CHECK_STR(jv_str(jv_get(jv_at(d, 1), "resourcePath")), "/users");
  /* a model's name in $ref is not a file */
  CHECK_STR(jv_str(jv_get(jv_get(jv_get(jv_get(jv_get(jv_at(d, 1), "models"), "U"), "properties"), "p"), "$ref")), "V");
  CHECK(!r.broken_refs);
  jv_free(v);
  free(p);
  oas_load_report_free(&r);
}

/* ---- the model ---- */

static int read_fixture(const char *name, oas_api *a)
{
  oas_load_report r;
  char *p = fix(name), err[512] = "";
  jv *v = oas_load(p, &r, err, sizeof(err));
  int rc = -1;
  memset(a, 0, sizeof(*a));
  if(v)
    rc = oas_read(v, a, err, sizeof(err));
  if(rc)
    printf("cannot read %s: %s\n", name, err);
  jv_free(v);
  free(p);
  oas_load_report_free(&r);
  return rc;
}

static int read_text(const char *yaml, oas_api *a, char *err, size_t n)
{
  jv *v = yaml_parse(yaml, strlen(yaml), err, n);
  int rc;
  memset(a, 0, sizeof(*a));
  if(!v)
    return -1;
  rc = oas_read(v, a, err, n);
  jv_free(v);
  return rc;
}

static oas_op *op(oas_api *a, const char *method, const char *path)
{
  int i;
  for(i = 0; i < a->nops; i++)
    if(!strcmp(a->ops[i].method, method) && !strcmp(a->ops[i].path, path))
      return &a->ops[i];
  printf("no operation %s %s\n", method, path);
  g_fail++;
  return NULL;
}

static const oas_field *field(const oas_fields *l, const char *name)
{
  int i;
  for(i = 0; i < l->n; i++)
    if(!strcmp(l->f[i].name, name))
      return &l->f[i];
  return NULL;
}

static void dump_fields(FILE *f, const char *what, const oas_fields *l)
{
  int i;
  for(i = 0; i < l->n; i++)
    fprintf(f, "%s %s=%s req=%d file=%d\n", what, l->f[i].name, l->f[i].value ? l->f[i].value : "(none)", l->f[i].required, l->f[i].is_file);
}

/* The whole model as text, to compare versions. */
static char *dump(const oas_api *a)
{
  char *path = fs_join(g_scratch, "dump.txt");
  FILE *f;
  int i, k;
  char *text;
  file_make_parents(path);
  f = fopen(path, "wb");
  fprintf(f, "%s\n", a->title);
  for(i = 0; i < a->nservers; i++) {
    fprintf(f, "server %s %s\n", a->servers[i].name, a->servers[i].url);
    dump_fields(f, "var", &a->servers[i].vars);
  }
  for(i = 0; i < a->nops; i++) {
    const oas_op *o = &a->ops[i];
    fprintf(f, "%s %s [%s]", o->method, o->path, o->name);
    for(k = 0; k < o->nfolders; k++)
      fprintf(f, " /%s", o->folders[k]);
    fprintf(f, " body=%d type=%s text=%s auth=%d %s raw=%s\n", (int)o->body, o->content_type ? o->content_type : "", o->body_text ? o->body_text : "",
            (int)o->auth, o->auth_name ? o->auth_name : "", o->raw_query ? o->raw_query : "");
    dump_fields(f, "q", &o->query);
    dump_fields(f, "h", &o->headers);
    dump_fields(f, "c", &o->cookies);
    dump_fields(f, "b", &o->body_rows);
  }
  fprintf(f, "%d %d %d %d\n", a->callbacks, a->oauth, a->alternatives_dropped, a->xml_bodies);
  fclose(f);
  text = file_read_all(path, NULL, (size_t)1 << 24);
  free(path);
  return text;
}

static void same_result_each_3x(void)
{
  static const char *const files[] = { "v3.0.0.yaml", "v3.0.4.json", "v3.1.0.yaml", "v3.1.2.json", "v3.2.0.yaml", "v3.2.1.json" };
  oas_api a;
  char *first = NULL;
  size_t i;
  for(i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
    if(!read_fixture(files[i], &a)) {
      char *t = dump(&a);
      if(!first)
        first = t;
      else {
        CHECK_STR(t, first);
        free(t);
      }
    }
    else
      CHECK(0);
    oas_api_free(&a);
  }
  free(first);
}

static void mapping_3x(void)
{
  oas_api a;
  oas_op *o;
  const oas_field *f;
  if(read_fixture("v3.0.0.yaml", &a)) {
    CHECK(0);
    return;
  }
  CHECK_STR(a.title, "Pet Store");
  CHECK(a.nservers == 2);
  CHECK_STR(a.servers[0].name, "Production");
  CHECK_STR(a.servers[0].url, "https://{env}.example.com/v1");
  CHECK(a.servers[0].vars.n == 1);
  CHECK_STR(a.servers[0].vars.f[0].value, "api");
  CHECK_STR(a.servers[1].url, "http://localhost:8080");

  o = op(&a, "GET", "/pets");
  CHECK_STR(o->name, "List pets");
  CHECK(o->nfolders == 1);
  CHECK_STR(o->folders[0], "pets");
  f = field(&o->query, "limit");
  CHECK(f && f->required);
  CHECK_STR(f ? f->value : NULL, "10");
  f = field(&o->query, "tag");
  CHECK(f && !f->required && !f->value);
  CHECK(field(&o->headers, "X-Trace") != NULL);
  CHECK(field(&o->headers, "Accept") == NULL);     /* accept_header_param_ignored */
  CHECK(field(&o->cookies, "sid") != NULL);
  CHECK(o->auth == OAS_AUTH_BEARER);

  o = op(&a, "POST", "/pets");
  CHECK_STR(o->name, "createPet");
  CHECK(o->auth == OAS_AUTH_BASIC);                /* Basic in any case */
  CHECK(o->body == OAS_BODY_JSON);
  CHECK_STR(o->content_type, "application/json");
  CHECK(o->body_text && strstr(o->body_text, "\"Rex\"") && strstr(o->body_text, "\"age\""));

  o = op(&a, "GET", "/pets/{petId}");              /* bare name; path_level_params_merge */
  CHECK_STR(o->name, "GET /pets/{petId}");
  CHECK(o->nfolders == 0);
  CHECK(o->query.n == 1);
  f = field(&o->query, "verbose");
  CHECK(f && f->required);
  CHECK_STR(f ? f->value : NULL, "true");
  CHECK(o->auth == OAS_AUTH_NONE);                 /* security_empty_array_means_none */

  o = op(&a, "POST", "/upload");
  CHECK(o->body == OAS_BODY_FORM);
  f = field(&o->body_rows, "title");
  CHECK(f && !f->required && !f->is_file);
  CHECK_STR(f ? f->value : NULL, "mine");
  f = field(&o->body_rows, "file");
  CHECK(f && f->required && f->is_file);
  CHECK(o->auth == OAS_AUTH_KEY_QUERY);            /* {} first: anonymous may do, the key is offered */
  CHECK_STR(o->auth_name, "api_key");

  o = op(&a, "POST", "/login");
  CHECK(o->body == OAS_BODY_URLENC);
  CHECK(o->auth == OAS_AUTH_DIGEST);
  f = field(&o->body_rows, "keep");
  CHECK_STR(f ? f->value : NULL, "true");

  o = op(&a, "PUT", "/raw");
  CHECK(o->body == OAS_BODY_RAW);
  CHECK_STR(o->body_text, "hello");
  CHECK_STR(o->content_type, "text/plain");
  CHECK(o->auth == OAS_AUTH_OAUTH);                /* first_alternative_counted */

  o = op(&a, "PUT", "/bin");
  CHECK(o->body == OAS_BODY_FILE);
  CHECK_STR(o->content_type, "application/octet-stream");
  CHECK(o->auth == OAS_AUTH_MTLS);

  o = op(&a, "GET", "/k0");                        /* security_operation_over_top: the top applies here */
  CHECK(o->auth == OAS_AUTH_KEY_HEADER);
  CHECK_STR(o->auth_name, "X-Api-Key");
  o = op(&a, "GET", "/k1");
  CHECK(o->auth == OAS_AUTH_KEY_COOKIE);
  CHECK_STR(o->auth_name, "session");
  o = op(&a, "GET", "/k2");
  CHECK(o->auth == OAS_AUTH_OAUTH);

  CHECK(a.callbacks == 1);
  CHECK(a.alternatives_dropped == 1);  /* /raw: {} on /upload loses nothing */
  CHECK(a.oauth == 2);
  oas_api_free(&a);
}

static void versions_3x(void)
{
  oas_api a;
  char err[256];
  oas_op *o;
  /* t31_type_array_and_const, t31_examples_array */
  CHECK(!read_text("openapi: 3.1.0\ninfo: {title: T}\npaths:\n  /a:\n    post:\n      requestBody:\n        content:\n          application/json:\n"
                   "            schema:\n              type: object\n              properties:\n                x: {type: [string, 'null']}\n"
                   "                y: {type: ['null', integer]}\n                z: {const: 7}\n                w: {type: string, examples: [one, two]}\n"
                   "webhooks:\n  ping: {post: {responses: {}}}\n", &a, err, sizeof(err)));
  o = op(&a, "POST", "/a");
  CHECK(o && o->body_text && strstr(o->body_text, "\"x\"") && strstr(o->body_text, "\"string\"") && strstr(o->body_text, "\"y\": 0") && strstr(o->body_text, "\"z\": 7") && strstr(o->body_text, "\"one\""));
  CHECK(a.webhooks == 1);
  oas_api_free(&a);
  /* the example of a media type, and of examples */
  CHECK(!read_text("openapi: 3.0.0\ninfo: {title: T}\npaths:\n  /a:\n    post:\n      requestBody:\n        content:\n          application/vnd.x+json:\n"
                   "            examples:\n              one: {value: {k: 1}}\n", &a, err, sizeof(err)));
  o = op(&a, "POST", "/a");
  CHECK(o && o->body == OAS_BODY_JSON && o->body_text && strstr(o->body_text, "\"k\": 1"));
  CHECK_STR(o ? o->content_type : NULL, "application/vnd.x+json");
  oas_api_free(&a);
  /* t32_query_method, t32_additional_operations, t32_tag_parent_nests_folders, t32_querystring */
  CHECK(!read_text("openapi: 3.2.0\ninfo: {title: T}\ntags:\n  - {name: partner, parent: external}\n  - {name: external}\npaths:\n  /s:\n"
                   "    query:\n      tags: [partner]\n      parameters:\n        - {name: q, in: querystring, content: {text/plain: {}}}\n"
                   "    additionalOperations:\n      COPY: {summary: Copies, responses: {}}\n", &a, err, sizeof(err)));
  o = op(&a, "QUERY", "/s");
  CHECK(o && o->nfolders == 2);
  if(o && o->nfolders == 2) {
    CHECK_STR(o->folders[0], "external");
    CHECK_STR(o->folders[1], "partner");
  }
  CHECK_STR(o ? o->raw_query : NULL, "q");
  o = op(&a, "COPY", "/s");
  CHECK_STR(o ? o->name : NULL, "Copies");
  oas_api_free(&a);
  /* a tag that is its own parent does not hang */
  CHECK(!read_text("openapi: 3.2.0\ninfo: {title: T}\ntags:\n  - {name: a, parent: b}\n  - {name: b, parent: a}\npaths:\n  /s:\n    get: {tags: [a], responses: {}}\n", &a, err, sizeof(err)));
  CHECK(a.nops == 1);
  oas_api_free(&a);
  /* not one of them */
  CHECK(read_text("openapi: 4.0.0\ninfo: {title: T}\n", &a, err, sizeof(err)));
  CHECK(strstr(err, "4.0.0") != NULL);
  oas_api_free(&a);
  CHECK(read_text("a: 1\n", &a, err, sizeof(err)));
  oas_api_free(&a);
}

static void sample(const char *yaml, const char *json, int line)
{
  jv *s = yaml_parse(yaml, strlen(yaml), NULL, 0), *got, *want = jv_parse(json, strlen(json), NULL, 0);
  char *t = oas_sample(s), *a, *b = jv_write(want, 0, NULL);
  got = t ? jv_parse(t, strlen(t), NULL, 0) : NULL;
  a = got ? jv_write(got, 0, NULL) : NULL;
  g_checks++;
  if(!a || strcmp(a, b)) {
    g_fail++;
    printf("FAIL line %d: sample %s, want %s\n", line, t ? t : "(null)", b);
  }
  free(t);
  free(a);
  free(b);
  jv_free(s);
  jv_free(got);
  jv_free(want);
}

static void samples(void)
{
  /* sample_order */
  sample("{example: 1, default: 2, const: 3, enum: [4], type: string}", "1", __LINE__);
  sample("{default: 2, const: 3, enum: [4]}", "2", __LINE__);
  sample("{const: 3, enum: [4]}", "3", __LINE__);
  sample("{enum: [4, 5]}", "4", __LINE__);
  sample("{type: string}", "\"string\"", __LINE__);
  sample("{type: integer}", "0", __LINE__);
  sample("{type: number}", "0", __LINE__);
  sample("{type: boolean}", "false", __LINE__);
  sample("{type: array, items: {type: integer}}", "[0]", __LINE__);
  sample("{properties: {a: {type: boolean}}}", "{\"a\":false}", __LINE__);
  sample("{}", "{}", __LINE__);
  /* sample_oneof_first, sample_allof_merge */
  sample("{oneOf: [{type: integer}, {type: string}]}", "0", __LINE__);
  sample("{anyOf: [{type: string}, {type: integer}]}", "\"string\"", __LINE__);
  sample("{allOf: [{properties: {a: {type: integer}}}, {properties: {b: {type: string}}}]}", "{\"a\":0,\"b\":\"string\"}", __LINE__);
  /* sample_depth_limit: a schema twenty deep stops */
  {
    char y[2048] = "{type: integer}";
    char t[2200];
    int i, depth = 0;
    jv *s, *got;
    char *text;
    const jv *at;
    for(i = 0; i < 20; i++) {
      snprintf(t, sizeof(t), "{properties: {a: %s}}", y);
      memcpy(y, t, strlen(t) + 1);
    }
    s = yaml_parse(y, strlen(y), NULL, 0);
    text = oas_sample(s);
    got = text ? jv_parse(text, strlen(text), NULL, 0) : NULL;
    for(at = got; jv_kind(at) == JV_OBJ; at = jv_get(at, "a"))
      depth++;
    CHECK(got != NULL && depth == 12);
    CHECK(jv_kind(at) == JV_NULL);
    free(text);
    jv_free(s);
    jv_free(got);
  }
}

static void swagger2(void)
{
  oas_api a, b;
  oas_op *o;
  const oas_field *f;
  char err[256];
  if(read_fixture("v2.0.yaml", &a) || read_fixture("v2.0.json", &b)) {
    CHECK(0);
    return;
  }
  {
    char *x = dump(&a), *y = dump(&b);
    CHECK_STR(x, y);
    free(x);
    free(y);
  }
  oas_api_free(&b);
  CHECK_STR(a.title, "Pet Store");
  /* s2_host_basepath_schemes */
  CHECK(a.nservers == 2);
  CHECK_STR(a.servers[0].url, "https://api.example.com/v1");
  CHECK_STR(a.servers[1].url, "http://api.example.com/v1");
  o = op(&a, "GET", "/pets");
  CHECK_STR(o->name, "List pets");
  CHECK_STR(o->folders[0], "pets");
  f = field(&o->query, "limit");
  CHECK(f && f->required);
  CHECK_STR(f ? f->value : NULL, "10");
  CHECK(field(&o->headers, "Accept") == NULL);
  CHECK(field(&o->headers, "X-Trace") != NULL);
  /* s2_body_param, through a definition */
  o = op(&a, "POST", "/pets");
  CHECK(o->body == OAS_BODY_JSON && o->body_text && strstr(o->body_text, "\"Rex\""));
  CHECK_STR(o->content_type, "application/json");
  CHECK(o->auth == OAS_AUTH_BASIC);
  o = op(&a, "GET", "/pets/{petId}");
  f = field(&o->query, "verbose");
  CHECK(o->query.n == 1 && f && f->required);
  CHECK(o->auth == OAS_AUTH_NONE);
  /* s2_formdata_file, s2_consumes_operation_over_top */
  o = op(&a, "POST", "/upload");
  CHECK(o->body == OAS_BODY_FORM);
  f = field(&o->body_rows, "file");
  CHECK(f && f->required && f->is_file);
  f = field(&o->body_rows, "title");
  CHECK_STR(f ? f->value : NULL, "mine");
  CHECK(o->auth == OAS_AUTH_KEY_QUERY);
  o = op(&a, "POST", "/login");
  CHECK(o->body == OAS_BODY_URLENC);
  o = op(&a, "PUT", "/raw");
  CHECK(o->body == OAS_BODY_RAW);
  CHECK_STR(o->body_text, "hello");
  CHECK_STR(o->content_type, "text/plain");
  CHECK(o->auth == OAS_AUTH_OAUTH);
  /* s2_security_definitions: top level applies */
  o = op(&a, "GET", "/k0");
  CHECK(o->auth == OAS_AUTH_KEY_HEADER);
  CHECK_STR(o->auth_name, "X-Api-Key");
  CHECK(a.alternatives_dropped == 1);
  oas_api_free(&a);
  /* s2_no_host_empty_base */
  CHECK(!read_text("swagger: '2.0'\ninfo: {title: T}\npaths: {}\n", &a, err, sizeof(err)));
  CHECK(a.nservers == 0);
  oas_api_free(&a);
  CHECK(!read_text("swagger: '2.0'\ninfo: {title: T}\nbasePath: /v2\npaths: {}\n", &a, err, sizeof(err)));
  CHECK(a.nservers == 1);
  CHECK_STR(a.nservers ? a.servers[0].url : NULL, "/v2");
  oas_api_free(&a);
  /* no schemes: https */
  CHECK(!read_text("swagger: 2.0\ninfo: {title: T}\nhost: h\npaths: {}\n", &a, err, sizeof(err)));
  CHECK_STR(a.nservers ? a.servers[0].url : NULL, "https://h");
  oas_api_free(&a);
  CHECK(read_text("swagger: '9.9'\n", &a, err, sizeof(err)));
  CHECK(strstr(err, "9.9") != NULL);
  oas_api_free(&a);
}

static void swagger1(void)
{
  oas_api a;
  oas_op *o;
  const oas_field *f;
  char err[256];
  static const char *const old[] = { "v1.1/api-docs.json", "v1.0/api-docs.json" };
  size_t i;
  /* s12_folder */
  if(read_fixture("v1.2/api-docs.json", &a)) {
    CHECK(0);
    return;
  }
  CHECK_STR(a.title, "Pet Store");
  CHECK(a.nservers == 1);
  CHECK_STR(a.nservers ? a.servers[0].url : NULL, "https://api.example.com/v1");
  o = op(&a, "GET", "/pets");
  CHECK_STR(o->name, "List pets");
  CHECK(o->nfolders == 1);
  CHECK_STR(o->nfolders ? o->folders[0] : NULL, "pets");           /* s1x_resource_folder_per_declaration */
  /* s12_paramtype_each */
  f = field(&o->query, "limit");
  CHECK(f && f->required);
  CHECK_STR(f ? f->value : NULL, "10");
  f = field(&o->query, "status");
  CHECK_STR(f ? f->value : NULL, "open");
  CHECK(field(&o->headers, "X-Trace") != NULL);
  /* s12_authorizations */
  CHECK(o->auth == OAS_AUTH_KEY_HEADER);
  CHECK_STR(o->auth_name, "X-Api-Key");
  o = op(&a, "POST", "/pets");
  CHECK_STR(o->name, "createPet");
  CHECK(o->auth == OAS_AUTH_BASIC);
  CHECK(o->body == OAS_BODY_JSON);
  CHECK(o->body_text && strstr(o->body_text, "\"name\"") && strstr(o->body_text, "\"owner\"") && strstr(o->body_text, "\"tags\""));
  o = op(&a, "GET", "/pets/{petId}.json");
  CHECK(o->auth == OAS_AUTH_KEY_QUERY);
  CHECK_STR(o->auth_name, "api_key");
  o = op(&a, "POST", "/upload");
  CHECK(o->body == OAS_BODY_FORM);
  f = field(&o->body_rows, "file");
  CHECK(f && f->is_file && f->required);
  CHECK(o->auth == OAS_AUTH_OAUTH);
  o = op(&a, "DELETE", "/users");
  CHECK_STR(o->folders[0], "users");
  oas_api_free(&a);
  /* s11_httpmethod_and_nickname, s10_same_shape */
  for(i = 0; i < 2; i++) {
    if(read_fixture(old[i], &a)) {
      CHECK(0);
      continue;
    }
    CHECK_STR(a.nservers ? a.servers[0].url : NULL, "http://old.example.com/api");
    o = op(&a, "GET", "/pets.json/{id}");
    CHECK_STR(o->name, "Get one");
    f = field(&o->query, "full");
    CHECK_STR(f ? f->value : NULL, "true");
    o = op(&a, "PUT", "/pets.json/{id}");
    CHECK_STR(o->name, "putPet");
    CHECK(o->body == OAS_BODY_JSON && o->body_text && strstr(o->body_text, "\"name\""));
    oas_api_free(&a);
  }
  /* one file holding a declaration */
  CHECK(!read_text("{\"swaggerVersion\":\"1.1\",\"apis\":[{\"path\":\"/a\",\"operations\":[{\"httpMethod\":\"GET\",\"nickname\":\"a\"}]}]}", &a, err, sizeof(err)));
  CHECK(a.nops == 1 && a.nservers == 0);
  oas_api_free(&a);
  /* s1x_other_shape_refused */
  CHECK(read_text("{\"swaggerVersion\":\"1.0\",\"resources\":[1],\"other\":2}", &a, err, sizeof(err)));
  CHECK(strstr(err, "1.0") && strstr(err, "resources"));
  oas_api_free(&a);
  CHECK(read_text("{\"swaggerVersion\":\"1.9\",\"apis\":[]}", &a, err, sizeof(err)));
  CHECK(strstr(err, "1.9") != NULL);
  oas_api_free(&a);
}

int main(int argc, char **argv)
{
  if(argc < 3) {
    fprintf(stderr, "usage: test_openapi FIXTURES SCRATCH\n");
    return 2;
  }
  g_fix = argv[1];
  g_scratch = argv[2];
  ref_same_file();
  ref_other_file_relative();
  ref_siblings_merge();
  ref_cycle_stops();
  ref_remote_counted();
  ref_broken_counted();
  ref_outside_folder_listed();
  load_limit_files();
  load_limit_bytes();
  load_refusals();
  swagger12_folder();
  same_result_each_3x();
  mapping_3x();
  versions_3x();
  samples();
  swagger2();
  swagger1();
  printf("%d checks, %d failed\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
