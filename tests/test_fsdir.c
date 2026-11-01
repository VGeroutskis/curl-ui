/* The picker's view of the file system, and how a chosen path enters an option's value. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fileio.h"
#include "fsdir.h"
#include "options.h"

#ifdef _WIN32
#include <windows.h>
#endif

/* The test program has no UTF-8 code page, unlike the app: a Greek name takes the wide call. */
static int write_named(const char *path)
{
#ifdef _WIN32
  wchar_t w[1024];
  FILE *f;
  MultiByteToWideChar(CP_UTF8, 0, path, -1, w, 1024);
  f = _wfopen(w, L"wb");
  if(!f)
    return -1;
  fputs("12345", f);
  fclose(f);
  return 0;
#else
  return file_write_private(path, "12345", 5);
#endif
}

static int fails;
#define CHECK(c) do { if(!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while(0)
#define CHECK_STR(a, b) do { const char *x_ = (a), *y_ = (b); \
    if(!x_ || strcmp(x_, y_)) { fprintf(stderr, "%s:%d: got [%s] want [%s]\n", __FILE__, __LINE__, x_ ? x_ : "(null)", y_); fails++; } } while(0)

static void value_is(const char *opt, const char *old, const char *path, const char *want)
{
  char *v = fs_option_value(opt, old, path);
  CHECK_STR(v, want);
  free(v);
}

static void test_listing(const char *scratch)
{
  fs_listing l;
  char err[256];
  char *base = fs_join(scratch, "pick"), *p;
  p = fs_join(base, "x");
  file_make_parents(p);
  free(p);
  /* a Greek name, upper and lower case, a folder that sorts after the files by name */
  static const char *const files[] = { "b.txt", "A.txt", "\xCE\xB1\xCF\x81\xCF\x87\xCE\xB5\xCE\xAF\xCE\xBF.json", ".secret" };
  size_t i;
  for(i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
    p = fs_join(base, files[i]);
    CHECK(!write_named(p));
    free(p);
  }
  p = fs_join(base, "zdir/x");
  CHECK(!file_write_private(p, "", 0));
  free(p);

  CHECK(!fs_list(base, &l, err, sizeof(err)));
  CHECK(l.n == 5);
  CHECK(l.e[0].is_dir && !strcmp(l.e[0].name, "zdir"));
  { /* files by name, whatever the case */
    int a = -1, b = -1, g = -1, s = -1, k;
    for(k = 0; k < l.n; k++) {
      if(!strcmp(l.e[k].name, "A.txt")) a = k;
      if(!strcmp(l.e[k].name, "b.txt")) b = k;
      if(!strcmp(l.e[k].name, files[2])) g = k;
      if(!strcmp(l.e[k].name, ".secret")) s = k;
    }
    CHECK(a >= 0 && b == a + 1 && g > b && s >= 0);
    CHECK(l.e[a].size == 5 && !l.e[a].is_dir && l.e[a].mtime > 0 && !l.e[a].hidden);
    CHECK(g >= 0 && l.e[g].size == 5);
#ifndef _WIN32
    CHECK(l.e[s].hidden);
#endif
  }
  fs_listing_free(&l);
  CHECK(l.n == 0 && l.e == NULL);

  p = fs_join(base, "nope");
  err[0] = '\0';
  CHECK(fs_list(p, &l, err, sizeof(err)) == -1);
  CHECK(err[0] != '\0');
  CHECK(fs_kind(p) == FS_MISSING);
  free(p);
  CHECK(fs_kind(base) == FS_DIR);
  p = fs_join(base, "b.txt");
  CHECK(fs_kind(p) == FS_FILE);
  err[0] = '\0';
  CHECK(fs_list(p, &l, err, sizeof(err)) == -1);  /* a file is not a folder */
  free(p);
  free(base);
}

static void test_paths(void)
{
  char *s;
#ifdef _WIN32
  CHECK(fs_is_absolute("C:\\x") && fs_is_absolute("\\\\srv\\share") && fs_is_absolute("c:/x") && !fs_is_absolute("x\\y"));
  s = fs_parent("C:\\a\\b"); CHECK_STR(s, "C:\\a"); free(s);
  s = fs_parent("C:\\a"); CHECK_STR(s, "C:\\"); free(s);
  CHECK(fs_parent("C:\\") == NULL);
  s = fs_join("C:\\", "x"); CHECK_STR(s, "C:\\x"); free(s);
  s = fs_join("C:\\a", "x"); CHECK_STR(s, "C:\\a\\x"); free(s);
  s = fs_resolve("C:\\a", "D:\\b"); CHECK_STR(s, "D:\\b"); free(s);
  CHECK_STR(fs_basename("C:\\a\\b.txt"), "b.txt");
#else
  CHECK(fs_is_absolute("/x") && !fs_is_absolute("x/y") && !fs_is_absolute(""));
  s = fs_parent("/a/b"); CHECK_STR(s, "/a"); free(s);
  s = fs_parent("/a/b/"); CHECK_STR(s, "/a"); free(s);
  s = fs_parent("/a"); CHECK_STR(s, "/"); free(s);
  CHECK(fs_parent("/") == NULL);
  s = fs_join("/", "x"); CHECK_STR(s, "/x"); free(s);
  s = fs_join("/a", "x"); CHECK_STR(s, "/a/x"); free(s);
  s = fs_join("/a/", "x"); CHECK_STR(s, "/a/x"); free(s);
  s = fs_resolve("/a", "b.txt"); CHECK_STR(s, "/a/b.txt"); free(s);
  s = fs_resolve("/a", "/c/d"); CHECK_STR(s, "/c/d"); free(s);
  if(fs_home()) {
    char want[1024];
    snprintf(want, sizeof(want), "%s/x", fs_home());
    s = fs_resolve("/a", "~/x"); CHECK_STR(s, want); free(s);
    s = fs_resolve("/a", "~"); CHECK_STR(s, fs_home()); free(s);
  }
  CHECK_STR(fs_basename("/a/b.txt"), "b.txt");
  CHECK_STR(fs_basename("b.txt"), "b.txt");
  s = fs_canon("/usr/../usr/./bin/"); CHECK(s && fs_is_absolute(s) && !strstr(s, "..") && !strstr(s, "/./")); free(s);
  s = fs_canon("/no-such-dir-xyz/../no-such-file"); CHECK_STR(s, "/no-such-file"); free(s);
  s = fs_canon("relative-no-such"); CHECK(s && fs_is_absolute(s)); free(s);
#endif
  CHECK(fs_name_matches("Report.JSON", "json"));
  CHECK(fs_name_matches("anything", ""));
  CHECK(!fs_name_matches("a.txt", "json"));
}

static void test_options(void)
{
  fs_value_form form;
  int i;
  static const struct { const char *name; fs_pick_mode mode; fs_value_form form; } want[] = {
    { "cacert", FS_PICK_OPEN, FS_VALUE_PLAIN }, { "config", FS_PICK_OPEN, FS_VALUE_PLAIN },
    { "upload-file", FS_PICK_OPEN, FS_VALUE_PLAIN }, { "key", FS_PICK_OPEN, FS_VALUE_PLAIN },
    { "cookie", FS_PICK_OPEN, FS_VALUE_PLAIN }, { "netrc-file", FS_PICK_OPEN, FS_VALUE_PLAIN },
    { "cert", FS_PICK_OPEN, FS_VALUE_CERT }, { "proxy-cert", FS_PICK_OPEN, FS_VALUE_CERT },
    { "data", FS_PICK_OPEN, FS_VALUE_AT }, { "json", FS_PICK_OPEN, FS_VALUE_AT },
    { "data-binary", FS_PICK_OPEN, FS_VALUE_AT }, { "header", FS_PICK_OPEN, FS_VALUE_AT },
    { "data-urlencode", FS_PICK_OPEN, FS_VALUE_NAME_AT }, { "variable", FS_PICK_OPEN, FS_VALUE_NAME_AT },
    { "form", FS_PICK_OPEN, FS_VALUE_FORM },
    { "output", FS_PICK_SAVE, FS_VALUE_PLAIN }, { "dump-header", FS_PICK_SAVE, FS_VALUE_PLAIN },
    { "cookie-jar", FS_PICK_SAVE, FS_VALUE_PLAIN }, { "trace", FS_PICK_SAVE, FS_VALUE_PLAIN },
    { "stderr", FS_PICK_SAVE, FS_VALUE_PLAIN }, { "etag-save", FS_PICK_SAVE, FS_VALUE_PLAIN },
    { "hsts", FS_PICK_SAVE, FS_VALUE_PLAIN }, { "alt-svc", FS_PICK_SAVE, FS_VALUE_PLAIN },
    { "capath", FS_PICK_DIR, FS_VALUE_PLAIN }, { "proxy-capath", FS_PICK_DIR, FS_VALUE_PLAIN },
    { "output-dir", FS_PICK_DIR, FS_VALUE_PLAIN },
    { "url", FS_PICK_NONE, FS_VALUE_PLAIN }, { "user-agent", FS_PICK_NONE, FS_VALUE_PLAIN },
    { "data-raw", FS_PICK_NONE, FS_VALUE_PLAIN }, { "form-string", FS_PICK_NONE, FS_VALUE_PLAIN },
    { "request-target", FS_PICK_NONE, FS_VALUE_PLAIN },
  };
  for(i = 0; i < (int)(sizeof(want) / sizeof(want[0])); i++) {
    fs_pick_mode m = fs_option_mode(want[i].name, &form);
    if(m != want[i].mode || (m != FS_PICK_NONE && form != want[i].form)) {
      fprintf(stderr, "wrong mode for --%s\n", want[i].name);
      fails++;
    }
  }
  /* every option curl itself says takes a file or folder has a way to choose one */
  for(i = 0; i < g_option_count; i++) {
    const char *arg = g_options[i].arg;
    if((!strcmp(arg, "<file>") || !strcmp(arg, "<filename>") || !strcmp(arg, "<dir>")) &&
       fs_option_mode(g_options[i].name, &form) == FS_PICK_NONE) {
      fprintf(stderr, "--%s %s has no picker\n", g_options[i].name, arg);
      fails++;
    }
  }

  value_is("cacert", "old", "/p/a b.pem", "/p/a b.pem");
  value_is("data", "a=1", "/p/x", "@/p/x");
  value_is("header", "", "/p/x", "@/p/x");
  value_is("data-urlencode", "", "/p/x", "@/p/x");
  value_is("data-urlencode", "name@/old", "/p/x", "name@/p/x");
  value_is("data-urlencode", "name=val", "/p/x", "name@/p/x");
  value_is("data-urlencode", "@/old", "/p/x", "@/p/x");
  value_is("variable", "%name@/old", "/p/x", "%name@/p/x");
  value_is("variable", "name=text", "/p/x", "name@/p/x");
  value_is("form", "up=@/old", "/p/x", "up=@/p/x");
  value_is("form", "up=text", "/p/x", "up=@/p/x");
  value_is("form", "", "/p/x", "file=@/p/x");
  value_is("form", "up=@/old;type=text/plain", "/p/x", "up=@/p/x");
  value_is("form", "up=", "/p/a;b.txt", "up=@\"/p/a;b.txt\"");
  value_is("form", "up=", "/p/a\"b", "up=@\"/p/a\\\"b\"");
  value_is("cert", "", "/p/c.pem", "/p/c.pem");
  value_is("cert", "/old.pem:s3cret", "/p/c.pem", "/p/c.pem:s3cret");
  value_is("cert", "/old\\:x.pem:s3cret", "/p/c.pem", "/p/c.pem:s3cret");
  value_is("cert", "/old.pem:a:b", "/p/c:d.pem", "/p/c\\:d.pem:a:b");
  value_is("cert", "", "/p/c\\d.pem", "/p/c\\\\d.pem");
  value_is("cert", "C:\\old.pem:pw", "C:\\new.pem", "C:\\\\new.pem:pw");

  { /* where the picker starts */
    char *s;
    s = fs_option_path("cacert", "/p/a.pem"); CHECK_STR(s, "/p/a.pem"); free(s);
    s = fs_option_path("data", "@/p/x"); CHECK_STR(s, "/p/x"); free(s);
    s = fs_option_path("data", "a=1"); CHECK_STR(s, ""); free(s);
    s = fs_option_path("form", "up=@/p/x;type=a/b"); CHECK_STR(s, "/p/x"); free(s);
    s = fs_option_path("form", "up=@\"/p/a;b\""); CHECK_STR(s, "/p/a;b"); free(s);
    s = fs_option_path("cert", "/p/c\\:d.pem:pw"); CHECK_STR(s, "/p/c:d.pem"); free(s);
    s = fs_option_path("data-urlencode", "n@/p/x"); CHECK_STR(s, "/p/x"); free(s);
    s = fs_option_path("user-agent", "x"); CHECK_STR(s, ""); free(s);
  }
}

static void test_hit(void)
{
  /* a 1000x600 window with a 48 high bar and its controls from x=800 */
  #define HIT(x, y) win_hit_test(x, y, 1000, 600, 48, 800, 0)
  CHECK(HIT(300, 20) == WIN_HIT_DRAG);
  CHECK(HIT(799, 47) == WIN_HIT_DRAG);
  CHECK(HIT(800, 20) == WIN_HIT_NONE);    /* the buttons take their clicks */
  CHECK(HIT(300, 48) == WIN_HIT_NONE);
  CHECK(HIT(500, 300) == WIN_HIT_NONE);
  CHECK(HIT(0, 300) == WIN_HIT_W);
  CHECK(HIT(999, 300) == WIN_HIT_E);
  CHECK(HIT(500, 0) == WIN_HIT_N);
  CHECK(HIT(500, 599) == WIN_HIT_S);
  CHECK(HIT(0, 0) == WIN_HIT_NW);
  CHECK(HIT(999, 0) == WIN_HIT_NE);
  CHECK(HIT(0, 599) == WIN_HIT_SW);
  CHECK(HIT(999, 599) == WIN_HIT_SE);
  /* a maximized window has no edges to pull, and a close button in the corner stays one */
  CHECK(win_hit_test(0, 0, 1000, 600, 48, 800, 1) == WIN_HIT_DRAG);
  CHECK(win_hit_test(999, 0, 1000, 600, 48, 800, 1) == WIN_HIT_NONE);
  CHECK(win_hit_test(0, 300, 1000, 600, 48, 800, 1) == WIN_HIT_NONE);

}

static void touch(const char *base, const char *name)
{
  char *p = fs_join(base, name);
  CHECK(!file_write_private(p, "", 0));
  free(p);
}

static void test_libdecor(const char *scratch)
{
  char *base = fs_join(scratch, "decor");
  char *none = fs_join(base, "none"), *noplug = fs_join(base, "noplug"), *good = fs_join(base, "good"), *other = fs_join(base, "other");
  char lib[1024], plugins[1024], want[1200];
  const char *roots[4];
  touch(noplug, "libdecor-0.so.0");       /* the library, but its plugins folder is empty of plugins */
  touch(noplug, "libdecor/plugins-1/readme.txt");
  touch(good, "libdecor-0.so.0");
  touch(good, "libdecor/plugins-1/libdecor-gtk.so");
  touch(other, "libdecor/plugins-1/libdecor-gtk.so");  /* plugins with no library */
  roots[0] = none;
  roots[1] = noplug;
  roots[2] = other;
  CHECK(!libdecor_find_in(roots, 3, lib, sizeof(lib), plugins, sizeof(plugins)));
  roots[3] = good;
  CHECK(libdecor_find_in(roots, 4, lib, sizeof(lib), plugins, sizeof(plugins)));
  snprintf(want, sizeof(want), "%s/libdecor-0.so.0", good);
  CHECK_STR(lib, want);
  snprintf(want, sizeof(want), "%s/libdecor/plugins-1", good);
  CHECK_STR(plugins, want);
  CHECK(!libdecor_find_in(roots, 4, lib, 4, plugins, sizeof(plugins)));  /* no room: not found, not cut */
  free(base); free(none); free(noplug); free(good); free(other);
}

int main(int argc, char **argv)
{
  if(argc < 2) {
    fprintf(stderr, "usage: test_fsdir SCRATCH\n");
    return 2;
  }
  test_listing(argv[1]);
  test_paths();
  test_options();
  test_hit();
#ifndef _WIN32
  test_libdecor(argv[1]);
#endif
  if(fails)
    fprintf(stderr, "%d failed\n", fails);
  else
    puts("all fsdir tests passed");
  return fails ? 1 : 0;
}
