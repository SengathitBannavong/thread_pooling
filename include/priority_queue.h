#ifndef PRIORITY_QUEUE_H
#define PRIORITY_QUEUE_H

#include "task.h"
#include <pthread.h>
#include <stdatomic.h>

#define PQ_BITMASK_BITS 32

struct priority_queue_t {
  struct task_t   *heads[NUM_PRIORITIES];   /* FIFO with head and tail */
  struct task_t   *tails[NUM_PRIORITIES];
  uint32_t         ready_mask;              /* O(1) get highest task */
  uint64_t         size;                   
  pthread_mutex_t  mutex;                   /* Protect every field */
  pthread_cond_t   not_empty;               /* Signal when have task */
};


/**
 * call every time before using pq
 *
 * @return 0 = success, -1 = error (mutex/cond init error)
 */
int             pq_init(struct priority_queue_t *pq);

/**
 * if still have task this func will destroy all
 */
void            pq_destroy(struct priority_queue_t *pq);

/**
 * be carefull task is should not NULL
 */
void            pq_push(struct priority_queue_t *pq, struct task_t *task);

/**
 * @return NULL if is empty
 */
struct task_t   *pq_pop(struct priority_queue_t *pq);

/**
 * blocking pop that can abort when shutdown flag becomes true.
 * @return NULL when queue is empty and shutdown flag is true.
 */
struct task_t   *pq_pop_until_shutdown(struct priority_queue_t *pq,
                                       const atomic_bool *shutdown_flag);

/**
 * wake all threads waiting in pq_pop / pq_pop_until_shutdown
 */
void             pq_wake_all(struct priority_queue_t *pq);

/**
 * @return NULL if is empty
 */
struct task_t   *pq_pop_nonblock(struct priority_queue_t *pq);

int              pq_size(struct priority_queue_t *pq);
int              pq_is_empty(struct priority_queue_t *pq);

#endif