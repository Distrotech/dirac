
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <schroedinger/schro.h>
#include <schroedinger/schroasync.h>
#include <schroedinger/schrodebug.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

enum {
  STATE_IDLE,
  STATE_BUSY,
  STATE_STOP
};

struct _SchroAsync {
  int n_threads;
  int n_threads_running;
  int n_idle;

  volatile int n_completed;
  volatile int n_waiting;

  pthread_mutex_t mutex;
  pthread_cond_t app_cond;
  pthread_cond_t thread_cond;

  SchroThread *threads;

  SchroAsyncTask *list_first;
  SchroAsyncTask *list_last;

  SchroAsyncTask *done_first;
  SchroAsyncTask *done_last;

  int (*schedule) (void *);
  void *schedule_closure;
};

struct _SchroThread {
  pthread_t pthread;
  SchroAsync *async;
  int state;
  int index;
};

struct _SchroAsyncTask {
  SchroAsyncTask *next;
  SchroAsyncTask *prev;
  void (*func)(void *);
  void *priv;
};

static void * schro_thread_main (void *ptr);

SchroAsync *
schro_async_new(int n_threads, int (*schedule)(void *), void *closure)
{
  SchroAsync *async;
  pthread_attr_t attr;
  pthread_mutexattr_t mutexattr;
  pthread_condattr_t condattr;
  int i;

  if (n_threads == 0) {
    char *s;

    s = getenv ("SCHRO_THREADS");
    if (s && s[0]) {
      char *end;
      int n;
      n = strtoul (s, &end, 0);
      if (end[0] == 0) {
        n_threads = n;
      }
    }
    if (n_threads == 0) {
      n_threads = sysconf(_SC_NPROCESSORS_CONF);
    }
    if (n_threads == 0) {
      n_threads = 1;
    }
  }
  async = malloc(sizeof(SchroAsync));
  memset (async, 0, sizeof(SchroAsync));

  SCHRO_DEBUG("%d", n_threads);
  async->n_threads = n_threads;
  async->threads = malloc(sizeof(SchroThread) * n_threads);
  memset (async->threads, 0, sizeof(SchroThread) * n_threads);

  async->schedule = schedule;
  async->schedule_closure = closure;

  pthread_mutexattr_init (&mutexattr);
  pthread_mutex_init (&async->mutex, &mutexattr);
  pthread_condattr_init (&condattr);
  pthread_cond_init (&async->app_cond, &condattr);
  pthread_cond_init (&async->thread_cond, &condattr);

  pthread_attr_init (&attr);
  
  pthread_mutex_lock (&async->mutex);

  for(i=0;i<n_threads;i++){
    SchroThread *thread = async->threads + i;

    thread->async = async;
    thread->index = i;
    pthread_create (&async->threads[i].pthread, &attr,
        schro_thread_main, async->threads + i);
    pthread_mutex_lock (&async->mutex);
  }
  pthread_mutex_unlock (&async->mutex);

  pthread_attr_destroy (&attr);
  pthread_mutexattr_destroy (&mutexattr);
  pthread_condattr_destroy (&condattr);

  return async;
}

void
schro_async_free (SchroAsync *async)
{
  int i;

  pthread_mutex_lock (&async->mutex);
  for(i=0;i<async->n_threads;i++){
    async->threads[i].state = STATE_STOP;
  }
  while(async->n_threads_running > 0) {
    pthread_cond_signal (&async->thread_cond);
    pthread_cond_wait (&async->app_cond, &async->mutex);
  }
  pthread_mutex_unlock (&async->mutex);

  for(i=0;i<async->n_threads;i++){
    void *ignore;
    pthread_join (async->threads[i].pthread, &ignore);
  }

  free(async->threads);
  free(async);
}

void
schro_async_run_locked (SchroAsync *async, void (*func)(void *), void *ptr)
{
  SchroAsyncTask *atask;

  atask = malloc(sizeof(SchroAsyncTask));

  SCHRO_DEBUG("queueing task %p", atask);
  atask->func = func;
  atask->priv = ptr;

  atask->next = NULL;
  atask->prev = async->list_last;
  if (async->list_last) {
    async->list_last->next = atask;
  } else {
    async->list_first = atask;
  }
  async->list_last = atask;
  async->n_waiting++;

  SCHRO_DEBUG("waiting %d", async->n_waiting);

  pthread_cond_signal (&async->thread_cond);
}

int schro_async_get_num_waiting (SchroAsync *async)
{
  return async->n_waiting;
}

int schro_async_get_num_completed (SchroAsync *async)
{
  return async->n_completed;
}

void *schro_async_pull (SchroAsync *async)
{
  SchroAsyncTask *atask;
  void *ptr;

  pthread_mutex_lock (&async->mutex);
  if (!async->done_first) {
    pthread_mutex_unlock (&async->mutex);
    return NULL;
  }
  SCHRO_ASSERT(async->done_first != NULL);

  atask = async->done_first;
  async->done_first = atask->next;
  if (async->done_first) {
    async->done_first->prev = NULL;
  } else {
    async->done_last = NULL;
  }
  async->n_completed--;
  pthread_mutex_unlock (&async->mutex);

  ptr = atask->priv;
  free (atask);

  return ptr;
}

void *
schro_async_pull_locked (SchroAsync *async)
{
  SchroAsyncTask *atask;
  void *ptr;

  if (!async->done_first) {
    return NULL;
  }
  SCHRO_ASSERT(async->done_first != NULL);

  atask = async->done_first;
  async->done_first = atask->next;
  if (async->done_first) {
    async->done_first->prev = NULL;
  } else {
    async->done_last = NULL;
  }
  async->n_completed--;

  ptr = atask->priv;
  free (atask);

  return ptr;
}

void
schro_async_wait_locked (SchroAsync *async)
{
  pthread_cond_wait (&async->app_cond, &async->mutex);
}

void
schro_async_wait_one (SchroAsync *async)
{
  pthread_mutex_lock (&async->mutex);
  if (async->n_completed > 0) {
    pthread_mutex_unlock (&async->mutex);
    return;
  }

  pthread_cond_wait (&async->app_cond, &async->mutex);
  pthread_mutex_unlock (&async->mutex);
}

void
schro_async_wait (SchroAsync *async, int min_waiting)
{
  if (min_waiting < 1) min_waiting = 1;

  pthread_mutex_lock (&async->mutex);
  if (async->n_waiting < min_waiting) {
    pthread_mutex_unlock (&async->mutex);
    return;
  }
  if (async->n_completed > 0) {
    pthread_mutex_unlock (&async->mutex);
    return;
  }

  pthread_cond_wait (&async->app_cond, &async->mutex);
  pthread_mutex_unlock (&async->mutex);
}

static void *
schro_thread_main (void *ptr)
{
  SchroThread *thread = ptr;
  SchroAsync *async = thread->async;
  int ret;
  SchroAsyncTask *atask;

  /* thread starts with async->mutex locked */

  async->n_threads_running++;
  thread->state = STATE_IDLE;
  while (1) {
    switch (thread->state) {
      case STATE_IDLE:
        async->n_idle++;
        SCHRO_DEBUG("thread %d: idle", thread->index);
        pthread_cond_wait (&async->thread_cond, &async->mutex);
        SCHRO_DEBUG("thread %d: got signal", thread->index);
        async->n_idle--;
        if (thread->state == STATE_IDLE) {
          thread->state = STATE_BUSY;
        }
        break;
      case STATE_STOP:
        pthread_cond_signal (&async->app_cond);
        async->n_threads_running--;
        pthread_mutex_unlock (&async->mutex);
        SCHRO_DEBUG("thread %d: stopping", thread->index);
        return NULL;
      case STATE_BUSY:
        ret = async->schedule (async->schedule_closure);
        /* FIXME ignoring ret */
        if (!async->list_first) {
          thread->state = STATE_IDLE;
          break;
        }

        thread->state = STATE_BUSY;
        atask = async->list_first;

        async->list_first = atask->next;
        if (atask->next) {
          atask->next->prev = NULL;
        } else {
          async->list_last = NULL;
        }
        async->n_waiting--;

        if (async->n_idle > 0) {
          pthread_cond_signal (&async->thread_cond);
        }
        pthread_mutex_unlock (&async->mutex);

        SCHRO_DEBUG("thread %d: running", thread->index);
        atask->func (atask->priv);
        SCHRO_DEBUG("thread %d: done", thread->index);

        pthread_mutex_lock (&async->mutex);
      
        atask->prev = async->done_last;
        atask->next = NULL;
        if (async->done_last) {
          async->done_last->next = atask;
        } else {
          async->done_first = atask;
        }
        async->done_last = atask;
        async->n_completed++;

        pthread_cond_signal (&async->app_cond);

        break;
    }
  }
}

void schro_async_lock (SchroAsync *async)
{
  pthread_mutex_lock (&async->mutex);
}

void schro_async_unlock (SchroAsync *async)
{
  pthread_mutex_unlock (&async->mutex);
}

void schro_async_signal_scheduler (SchroAsync *async)
{
  pthread_cond_signal (&async->thread_cond);
}

