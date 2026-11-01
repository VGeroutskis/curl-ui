/* Minimal portable thread + mutex, header only. */
#ifndef CURLUI_OSTHREAD_H
#define CURLUI_OSTHREAD_H

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <process.h>

typedef HANDLE os_thread;
typedef CRITICAL_SECTION os_mutex;
#define OS_THREAD_FN unsigned __stdcall
#define OS_THREAD_RET 0
typedef unsigned (__stdcall *os_thread_fn)(void *);

static inline int os_thread_start(os_thread *t, os_thread_fn fn, void *arg)
{
  *t = (HANDLE)_beginthreadex(NULL, 0, fn, arg, 0, NULL);
  return *t ? 0 : -1;
}
static inline void os_thread_join(os_thread t)
{
  WaitForSingleObject(t, INFINITE);
  CloseHandle(t);
}
static inline void os_mutex_init(os_mutex *m) { InitializeCriticalSection(m); }
static inline void os_mutex_destroy(os_mutex *m) { DeleteCriticalSection(m); }
static inline void os_mutex_lock(os_mutex *m) { EnterCriticalSection(m); }
static inline void os_mutex_unlock(os_mutex *m) { LeaveCriticalSection(m); }
#else
#include <pthread.h>

typedef pthread_t os_thread;
typedef pthread_mutex_t os_mutex;
#define OS_THREAD_FN void *
#define OS_THREAD_RET NULL
typedef void *(*os_thread_fn)(void *);

static inline int os_thread_start(os_thread *t, os_thread_fn fn, void *arg)
{
  return pthread_create(t, NULL, fn, arg) ? -1 : 0;
}
static inline void os_thread_join(os_thread t) { pthread_join(t, NULL); }
static inline void os_mutex_init(os_mutex *m) { pthread_mutex_init(m, NULL); }
static inline void os_mutex_destroy(os_mutex *m) { pthread_mutex_destroy(m); }
static inline void os_mutex_lock(os_mutex *m) { pthread_mutex_lock(m); }
static inline void os_mutex_unlock(os_mutex *m) { pthread_mutex_unlock(m); }
#endif

#endif
