#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <stdatomic.h>
#include <pthread.h>
#include <stdbool.h>
#include "priority_queue.h"

struct thread_pool_t {
  pthread_t                 *workers;      /* array[num_workers] of thread ids */
  uint32_t                  num_workers;   /* total workers thread */
  struct priority_queue_t   pq;            /* shared priority queue */
  atomic_int                active_tasks;  /* tasks currently executing */
  atomic_bool               shutdown;      /* set true to begin shutdown */
  pthread_mutex_t           drain_mutex;   /* guards drain_cond */
  pthread_cond_t            drain_cond;    /* signalled when active_tasks == 0 */
};


/**
 * initialise pool and spawn worker threads.
 * @return 0 for success, -1 for error
 */
int       thread_pool_init(struct thread_pool_t *pool, int num_workers);

/**
 * create a task and push it onto the priority queue
 * @return task_id (>= 0) on success, -1 if pool is shutdown
 * or on allocation failure.
 */
int64_t   thread_pool_submit(struct thread_pool_t *pool, void(*task_fun_t)(void *arg),
                             void *arg, enum task_priority priority);

/**
 * graceful shutdown
 * 
 * waiting for every pending and active task to finish, then join all worker threads
 * and free resources. Block the caller untils done.
 * 
 * poll must not be NULL
 */
void      thread_pool_destroy(struct thread_pool_t *pool);


#endif