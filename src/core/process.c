#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#endif

#include "process.h"
#include "osthread.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define READ_CHUNK 16384

char *proc_win_cmdline(const char *const *argv)
{
  size_t cap = 1, n = 0;
  int i;
  char *out;
  for(i = 0; argv[i]; i++)
    cap += strlen(argv[i]) * 2 + 3;
  out = malloc(cap);
  if(!out)
    return NULL;
  for(i = 0; argv[i]; i++) {
    const char *a = argv[i];
    if(i)
      out[n++] = ' ';
    if(*a && !strpbrk(a, " \t\n\v\"")) {
      size_t len = strlen(a);
      memcpy(out + n, a, len);
      n += len;
      continue;
    }
    out[n++] = '"';
    for(;;) {
      size_t slashes = 0, k;
      while(*a == '\\') {
        a++;
        slashes++;
      }
      if(!*a) {
        /* backslashes before the closing quote must be doubled */
        for(k = 0; k < slashes * 2; k++)
          out[n++] = '\\';
        break;
      }
      if(*a == '"') {
        for(k = 0; k < slashes * 2 + 1; k++)
          out[n++] = '\\';
        out[n++] = '"';
      }
      else {
        for(k = 0; k < slashes; k++)
          out[n++] = '\\';
        out[n++] = *a;
      }
      a++;
    }
    out[n++] = '"';
  }
  out[n] = '\0';
  return out;
}

typedef struct {
  proc_sink sink;
  void *user;
#ifdef _WIN32
  HANDLE fd;
#else
  int fd;
#endif
} reader_arg;

struct proc {
  os_mutex lock;
  int finished;
  int joined;
  int exit_code;
  reader_arg out, err;
  os_thread out_thread, err_thread, waiter;
#ifdef _WIN32
  HANDLE process;
#else
  long pid;
#endif
};

static void set_finished(proc_t *p, int code)
{
  os_mutex_lock(&p->lock);
  p->exit_code = code;
  p->finished = 1;
  os_mutex_unlock(&p->lock);
}

int proc_finished(proc_t *p)
{
  int done;
  os_mutex_lock(&p->lock);
  done = p->finished;
  os_mutex_unlock(&p->lock);
  return done;
}

int proc_wait(proc_t *p)
{
  if(!p->joined) {
    os_thread_join(p->waiter);
    p->joined = 1;
  }
  return p->exit_code;
}

void proc_free(proc_t *p)
{
  if(!p)
    return;
  if(!proc_finished(p))
    proc_kill(p);
  proc_wait(p);
  os_mutex_destroy(&p->lock);
  free(p);
}

#ifdef _WIN32

static wchar_t *to_wide(const char *s)
{
  int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
  wchar_t *w = n > 0 ? malloc(sizeof(wchar_t) * (size_t)n) : NULL;
  if(w)
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
  return w;
}

static OS_THREAD_FN reader_main(void *arg)
{
  reader_arg *r = arg;
  char *buf = malloc(READ_CHUNK);
  DWORD got;
  while(buf && ReadFile(r->fd, buf, READ_CHUNK, &got, NULL) && got)
    r->sink(r->user, buf, got);
  free(buf);
  CloseHandle(r->fd);
  return OS_THREAD_RET;
}

static OS_THREAD_FN waiter_main(void *arg)
{
  proc_t *p = arg;
  DWORD code = 1;
  WaitForSingleObject(p->process, INFINITE);
  GetExitCodeProcess(p->process, &code);
  os_thread_join(p->out_thread);
  os_thread_join(p->err_thread);
  CloseHandle(p->process);
  set_finished(p, (int)code);
  return OS_THREAD_RET;
}

proc_t *proc_start(const char *const *argv, proc_sink on_out, proc_sink on_err,
                   void *user, char *err, size_t errlen)
{
  SECURITY_ATTRIBUTES sa;
  HANDLE out_r = NULL, out_w = NULL, err_r = NULL, err_w = NULL, nul;
  STARTUPINFOW si;
  PROCESS_INFORMATION pi;
  proc_t *p = NULL;
  char *cmd8 = NULL;
  wchar_t *cmd = NULL;
  BOOL ok;

  memset(&sa, 0, sizeof(sa));
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;
  nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                    OPEN_EXISTING, 0, NULL);
  if(nul == INVALID_HANDLE_VALUE || !CreatePipe(&out_r, &out_w, &sa, 0) ||
     !CreatePipe(&err_r, &err_w, &sa, 0)) {
    snprintf(err, errlen, "cannot create pipes (error %lu)", GetLastError());
    return NULL;
  }
  /* our ends must not leak into the child, or EOF never arrives */
  SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(err_r, HANDLE_FLAG_INHERIT, 0);

  cmd8 = proc_win_cmdline(argv);
  cmd = cmd8 ? to_wide(cmd8) : NULL;
  p = calloc(1, sizeof(*p));
  if(!cmd || !p) {
    snprintf(err, errlen, "out of memory");
    goto fail;
  }
  memset(&si, 0, sizeof(si));
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = nul;
  si.hStdOutput = out_w;
  si.hStdError = err_w;
  ok = CreateProcessW(NULL, cmd, NULL, NULL, TRUE,
                      CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, NULL, NULL,
                      &si, &pi);
  if(!ok) {
    snprintf(err, errlen, "cannot run %s (error %lu)", argv[0], GetLastError());
    goto fail;
  }
  CloseHandle(pi.hThread);
  CloseHandle(nul);
  CloseHandle(out_w);
  CloseHandle(err_w);
  free(cmd);
  free(cmd8);
  os_mutex_init(&p->lock);
  p->process = pi.hProcess;
  p->out.sink = on_out;
  p->out.user = user;
  p->out.fd = out_r;
  p->err.sink = on_err;
  p->err.user = user;
  p->err.fd = err_r;
  os_thread_start(&p->out_thread, reader_main, &p->out);
  os_thread_start(&p->err_thread, reader_main, &p->err);
  os_thread_start(&p->waiter, waiter_main, p);
  return p;

fail:
  CloseHandle(nul);
  CloseHandle(out_r);
  CloseHandle(out_w);
  CloseHandle(err_r);
  CloseHandle(err_w);
  free(cmd);
  free(cmd8);
  free(p);
  return NULL;
}

void proc_kill(proc_t *p)
{
  os_mutex_lock(&p->lock);
  if(!p->finished)
    TerminateProcess(p->process, 1);
  os_mutex_unlock(&p->lock);
}

#else /* POSIX */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

static OS_THREAD_FN reader_main(void *arg)
{
  reader_arg *r = arg;
  char *buf = malloc(READ_CHUNK);
  while(buf) {
    ssize_t got = read(r->fd, buf, READ_CHUNK);
    if(got > 0)
      r->sink(r->user, buf, (size_t)got);
    else if(got == 0 || errno != EINTR)
      break;
  }
  free(buf);
  close(r->fd);
  return OS_THREAD_RET;
}

static OS_THREAD_FN waiter_main(void *arg)
{
  proc_t *p = arg;
  int status = 0, code;
  pid_t res;
  do {
    res = waitpid((pid_t)p->pid, &status, 0);
  } while(res < 0 && errno == EINTR);
  if(res < 0)
    code = 127;
  else if(WIFEXITED(status))
    code = WEXITSTATUS(status);
  else if(WIFSIGNALED(status))
    code = PROC_EXIT_SIGNAL_BASE + WTERMSIG(status);
  else
    code = 127;
  os_thread_join(p->out_thread);
  os_thread_join(p->err_thread);
  set_finished(p, code);
  return OS_THREAD_RET;
}

static int make_pipe(int fds[2])
{
  if(pipe(fds))
    return -1;
  fcntl(fds[0], F_SETFD, FD_CLOEXEC);
  fcntl(fds[1], F_SETFD, FD_CLOEXEC);
  return 0;
}

proc_t *proc_start(const char *const *argv, proc_sink on_out, proc_sink on_err,
                   void *user, char *err, size_t errlen)
{
  int out_p[2], err_p[2], rc;
  pid_t pid;
  posix_spawn_file_actions_t fa;
  proc_t *p;

  if(make_pipe(out_p)) {
    snprintf(err, errlen, "pipe: %s", strerror(errno));
    return NULL;
  }
  if(make_pipe(err_p)) {
    snprintf(err, errlen, "pipe: %s", strerror(errno));
    close(out_p[0]);
    close(out_p[1]);
    return NULL;
  }
  p = calloc(1, sizeof(*p));
  posix_spawn_file_actions_init(&fa);
  posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
  posix_spawn_file_actions_adddup2(&fa, out_p[1], 1);
  posix_spawn_file_actions_adddup2(&fa, err_p[1], 2);
  rc = p ? posix_spawnp(&pid, argv[0], &fa, NULL, (char *const *)argv, environ) : ENOMEM;
  posix_spawn_file_actions_destroy(&fa);
  close(out_p[1]);
  close(err_p[1]);
  if(rc) {
    snprintf(err, errlen, "cannot run %s: %s", argv[0], strerror(rc));
    close(out_p[0]);
    close(err_p[0]);
    free(p);
    return NULL;
  }
  os_mutex_init(&p->lock);
  p->pid = (long)pid;
  p->out.sink = on_out;
  p->out.user = user;
  p->out.fd = out_p[0];
  p->err.sink = on_err;
  p->err.user = user;
  p->err.fd = err_p[0];
  os_thread_start(&p->out_thread, reader_main, &p->out);
  os_thread_start(&p->err_thread, reader_main, &p->err);
  os_thread_start(&p->waiter, waiter_main, p);
  return p;
}

void proc_kill(proc_t *p)
{
  os_mutex_lock(&p->lock);
  if(!p->finished)
    kill((pid_t)p->pid, SIGKILL);
  os_mutex_unlock(&p->lock);
}

#endif
