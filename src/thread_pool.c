#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include "thread_pool.h"

/* --- worker schedule --- */
static void* worker_func(void *arg) {
  struct thread_pool_t *pool = (struct thread_pool_t*)arg;

  for(;;) {
    struct task_t *task = pq_pop_until_shutdown(&pool->pq, &pool->shutdown);

    /* NULL -> time to exit */
    if(task == NULL) {
      break;
    }

    atomic_fetch_add_explicit(&pool->active_tasks, 1,
                              memory_order_relaxed);
    
    task->func(task->arg);
    task_destroy(task);

    int remaining = atomic_fetch_add_explicit(&pool->active_tasks, -1,
                                              memory_order_acq_rel) -1;
    
    /* if we just finished the last active task, wake pool_destroy() */
    if(remaining == 0) {
      pthread_mutex_lock(&pool->drain_mutex);

      pthread_cond_signal(&pool->drain_cond);

      pthread_mutex_unlock(&pool->drain_mutex);
    }
  }

  return NULL;
}

int thread_pool_init(struct thread_pool_t *pool, int num_workers) {
  if(pool == NULL || num_workers <= 0) {
    errno = EINVAL;
    return -1;
  }

  pool->num_workers = num_workers;
  atomic_store_explicit(&pool->active_tasks, 0,
                        memory_order_relaxed);
  atomic_store_explicit(&pool->shutdown, false,
                        memory_order_relaxed);
  
  if(pq_init(&pool->pq) != 0) {
    return -1;
  }

  if(pthread_mutex_init(&pool->drain_mutex, NULL) != 0) {
    pq_destroy(&pool->pq);
    return -1;
  }
  if(pthread_cond_init(&pool->drain_cond, NULL) != 0) {
    pthread_mutex_destroy(&pool->drain_mutex);
    pq_destroy(&pool->pq);
    return -1;
  }

  /* worker allocate */
  pool->workers = malloc((size_t)num_workers * sizeof(pthread_t));
  if(pool->workers == NULL) {
    pthread_cond_destroy(&pool->drain_cond);
    pthread_mutex_destroy(&pool->drain_mutex);
    pq_destroy(&pool->pq);
    return -1;
  }

  /* spawn workers */
  for(int i = 0; i < num_workers; i++) {
    if(pthread_create(&pool->workers[i], NULL, worker_func, pool) != 0) {
      /* mark shutdown for another already-running workers will stop */
      atomic_store_explicit(&pool->shutdown, true,
                            memory_order_release);
      pq_wake_all(&pool->pq);

      for(int j = 0; j < i; j++) {
        pthread_join(pool->workers[j], NULL);
      }
      pq_destroy(&pool->pq);
      free(pool->workers);
      pthread_cond_destroy(&pool->drain_cond);
      pthread_mutex_destroy(&pool->drain_mutex);
      return -1;
    }
  }

  return 0;
}


int64_t thread_pool_submit(struct thread_pool_t *pool, void(*task_fun_t)(void *arg),
                           void *arg, enum task_priority priority) {
  if (pool == NULL || task_fun_t == NULL) {
    return -1;
  }
  
  /* reject new work once shutdown has begun */
  if(atomic_load_explicit(&pool->shutdown, memory_order_acquire)) {
    return -1;
  }

  struct task_t *task = task_create(task_fun_t, arg, priority);
  if(task == NULL) {
    return -1;
  }

  int64_t id = (int64_t)task->task_id;
  pq_push(&pool->pq, task);
  return id;
}

void thread_pool_destroy(struct thread_pool_t *pool) {
  if(pool == NULL) {
    return;
  }

  atomic_store_explicit(&pool->shutdown, true,
                        memory_order_release);

  /* ensure workers blocked in pq_pop_until_shutdown wake up and exit */
  pq_wake_all(&pool->pq);

  pthread_mutex_lock(&pool->drain_mutex);

  while(pq_size(&pool->pq) > 0 ||
        atomic_load_explicit(&pool->active_tasks, memory_order_acquire) > 0) {
    pthread_cond_wait(&pool->drain_cond, &pool->drain_mutex);
  }

  pthread_mutex_unlock(&pool->drain_mutex);

  
  for(uint32_t i = 0; i < pool->num_workers; i++) {
    pthread_join(pool->workers[i], NULL);
  }
  
  pq_destroy(&pool->pq);
  free(pool->workers);
  pool->workers = NULL;

  pthread_cond_destroy(&pool->drain_cond);
  pthread_mutex_destroy(&pool->drain_mutex);
}