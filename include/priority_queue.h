#ifndef PRIORITY_QUEUE_H
#define PRIORITY_QUEUE_H

#include "task.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>

#define PQ_BITMASK_BITS 32

struct priority_queue_t {
        struct task_t *heads[NUM_PRIORITIES]; /* FIFO with head and tail */
        struct task_t *tails[NUM_PRIORITIES];
        uint32_t ready_mask; /* O(1) get highest task */
        uint64_t size;
        _Atomic uint64_t depth;     /* lockless mirror of size for fast pre-check */
        _Atomic uint64_t max_tasks; /* 0 = unbounded; reject push when size >= max */
        pthread_mutex_t mutex; /* Protect every field */
        pthread_cond_t not_empty; /* Signal when have task */
};

/**
 * call every time before using pq
 *
 * @return 0 = success, -1 = error (mutex/cond init error)
 */
int pq_init(struct priority_queue_t *pq);

/**
 * if still have task this func will destroy all
 */
void pq_destroy(struct priority_queue_t *pq);

/**
 * be carefull task is should not NULL
 */
void pq_push(struct priority_queue_t *pq, struct task_t *task);

/**
 * bounded push: insert only if the queue has room.
 * @return true on success, false if the queue is at capacity (max_tasks reached).
 * When max_tasks == 0 this is unbounded and always succeeds.
 */
bool pq_try_push(struct priority_queue_t *pq, struct task_t *task);

/**
 * set the maximum number of queued tasks (0 = unbounded).
 * Thread-safe; callable at any time. Lowering below the current depth does not
 * drop queued tasks, it only rejects new pushes until the queue drains.
 */
void pq_set_max_tasks(struct priority_queue_t *pq, uint64_t max_tasks);

/**
 * lockless approximate capacity check.
 * @return true if the queue is at/over capacity (and a cap is set).
 */
bool pq_is_full_approx(struct priority_queue_t *pq);

/**
 * @return NULL if is empty
 */
struct task_t *pq_pop(struct priority_queue_t *pq);

/**
 * blocking pop that can abort when shutdown flag becomes true.
 * @return NULL when queue is empty and shutdown flag is true.
 */
struct task_t *pq_pop_until_shutdown(struct priority_queue_t *pq, const atomic_bool *shutdown_flag);

/**
 * wake all threads waiting in pq_pop / pq_pop_until_shutdown
 */
void pq_wake_all(struct priority_queue_t *pq);

/**
 * @return NULL if is empty
 */
struct task_t *pq_pop_nonblock(struct priority_queue_t *pq);

/**
 * run scan old task to upscale priority
 */
void pq_aging(struct priority_queue_t *pq, const atomic_bool *shutdown_flag, const long promote_time_ms);

int pq_size(struct priority_queue_t *pq);
int pq_is_empty(struct priority_queue_t *pq);

#endif
