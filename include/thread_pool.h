#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <stdatomic.h>
#include <pthread.h>
#include <stdbool.h>
#include "priority_queue.h"

typedef struct thread_pool thread_pool_t;

/**
 * initialise pool and spawn worker threads.
 * @return pointer to pool on success, NULL for error
 */
thread_pool_t *thread_pool_init(int num_workers);

/**
 * create a task and push it onto the priority queue
 * @return task_id (>= 0) on success, -1 if pool is shutdown
 * or on allocation failure.
 */
int64_t thread_pool_submit(thread_pool_t *pool, void (*task_fun_t)(void *arg), void *arg, enum task_priority priority);

/**
 * graceful shutdown
 *
 * waiting for every pending and active task to finish, then join all worker threads
 * and free resources. Block the caller untils done.
 *
 * pool must not be NULL
 */
void thread_pool_destroy(thread_pool_t **ori_pool);

/**
 * safe cpu wait
 *
 * waiting for every pending and active task to finish,
 * and then return.
 *
 * if have set flag pause this wait function will overwrite
 * pause -> resume for worker done all task correctly
 * 
 * pool must not be NULL
 */
void thread_pool_wait(thread_pool_t *ori_pool);

/**
 * pause worker
 *
 * pause worker but main thread still can submit task
 *
 * pool mush not be NULL
 */
void thread_pool_pause(thread_pool_t *ori_pool);

/**
 * resume worker
 *
 * wake up all worker on thread pool for pop task
 */
void thread_pool_resume(thread_pool_t *ori_pool);

#endif
