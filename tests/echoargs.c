/* Test helper: prints each argument on its own line, wrapped in [ ]. On
   Windows it reads the wide command line, so it checks the real round trip
   through CreateProcessW + CommandLineToArgvW. --sleep blocks until killed. */
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <fcntl.h>
#include <io.h>
int main(void)
{
  int argc, i;
  wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  _setmode(1, _O_BINARY);
  if(argc > 1 && !wcscmp(argv[1], L"--sleep")) {
    printf("[--sleep]\n");
    fflush(stdout);
    for(;;)
      Sleep(1000);
  }
  for(i = 1; i < argc; i++) {
    char buf[4096];
    int n = WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, buf, sizeof(buf), NULL, NULL);
    printf("[%s]\n", n > 0 ? buf : "");
  }
  return 7;
}
#else
#include <unistd.h>
int main(int argc, char **argv)
{
  int i;
  for(i = 1; i < argc; i++)
    printf("[%s]\n", argv[i]);
  if(argc > 1 && !strcmp(argv[1], "--sleep")) {
    fflush(stdout);
    for(;;)
      sleep(1);
  }
  return 7;
}
#endif
