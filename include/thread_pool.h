#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <stdatomic.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include "priority_queue.h"
#include "worker.h"

typedef struct thread_pool thread_pool_t;

/**
 * initialise pool and spawn worker threads.
 * @return pointer to pool on success, NULL for error
 */
thread_pool_t *thread_pool_init(int num_workers);

/**
 * create a task and push it onto the priority queue
 * @return task_id (>= 0) on success, -1 on failure. On failure errno is set:
 *   EINVAL    - pool or task function is NULL
 *   ESHUTDOWN - pool is shutting down
 *   EAGAIN    - queue is full (see thread_pool_set_max_tasks); retryable
 *   ENOMEM    - task allocation failed
 */
int64_t thread_pool_submit(thread_pool_t *pool, void (*task_fun_t)(void *arg), void *arg, enum task_priority priority);

/**
 * set the maximum number of queued (pending, not-yet-running) tasks.
 * 0 = unbounded (the default). When the queue is full, thread_pool_submit()
 * rejects immediately, returning -1 with errno=EAGAIN; queued work is never
 * dropped. Thread-safe and callable at any time; lowering the cap below the
 * current depth only stops new submits until the queue drains.
 */
void thread_pool_set_max_tasks(thread_pool_t *pool, uint64_t max_tasks);

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

/**
 * get capacity of workers are busy
 */
uint16_t thread_pool_num_working(thread_pool_t *ori_pool);

/**
 * get an approximate count of queued, not-yet-running tasks.
 *
 * This is a read-only monitoring getter. It does not lock or walk the priority
 * queue, so the value may be slightly skewed under concurrent submit/pop.
 */
uint64_t thread_pool_queue_depth(thread_pool_t *pool);

/**
 * Start the ncurses real-time thread pool monitor in a background thread.
 * The monitor redraws every 100 ms. Safe to call only once; use
 * thread_pool_monitor_reattach() to resume after a detach.
 *
 * Keys:
 *   p / P  – pause the pool
 *   r / R  – resume the pool
 *   q / Q  – detach (same as h)
 *
 * pool must not be NULL.
 */
void thread_pool_monitor_start(thread_pool_t *pool);

/**
 * Detach the monitor: stop the thread and restore the terminal.
 * The monitor context (history, pool pointer) is kept alive in the pool so
 * that thread_pool_monitor_reattach() can resume later.
 *
 * return 0 on success, -1 if monitor is not currently attached.
 */
int thread_pool_monitor_detach(thread_pool_t *pool);

/**
 * Reattach the monitor after a detach or [q] key press.
 * Spawns a new monitor thread; ncurses re-enters via refresh() (no re-alloc).
 * Colors and layout settings from the previous session are still active.
 *
 * return 0 on success, -1 if already attached or monitor_start() was never
 *         called.
 */
int thread_pool_monitor_reattach(thread_pool_t *pool);

/**
 * return true if the monitor thread is currently running (attached).
 */
bool thread_pool_monitor_attached(thread_pool_t *pool);

/**
 * return true if not error spawn thread
 */
bool thread_pool_enable_aging(thread_pool_t *pool, const long interval_time_ms, const long promote_time_ms);

/**
 * return true if not error join thread
 *
 * warning: this api will wake up all pause
 */
bool thread_pool_disable_aging(thread_pool_t *pool);

#endif
