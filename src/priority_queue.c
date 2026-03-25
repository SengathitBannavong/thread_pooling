#include "priority_queue.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

static inline int _get_highest_priority(uint32_t mask)
{
        return PQ_BITMASK_BITS - __builtin_clz(mask) - 1;
}

static inline bool _shutdown_requested(const atomic_bool *shutdown_flag)
{
        return shutdown_flag && atomic_load_explicit(shutdown_flag, memory_order_acquire);
}

int pq_init(struct priority_queue_t *pq)
{
        if (!pq)
                return -1;

        memset(pq, 0, sizeof(struct priority_queue_t));

        if (pthread_mutex_init(&pq->mutex, NULL) != 0) {
                fprintf(stderr, "[pq] ERROR: pthread_mutex_init failed\n");
                return -1;
        }

        if (pthread_cond_init(&pq->not_empty, NULL) != 0) {
                fprintf(stderr, "[pq] ERROR: pthread_cond_init failed\n");
                pthread_mutex_destroy(&pq->mutex);
                return -1;
        }

        return 0;
}

void pq_destroy(struct priority_queue_t *pq)
{
        if (!pq)
                return;

        pthread_mutex_lock(&pq->mutex);

        for (int p = 0; p < NUM_PRIORITIES; p++) {
                struct task_t *curr = pq->heads[p];

                while (curr) {
                        struct task_t *next = curr->next;
                        task_destroy(curr);
                        curr = next;
                }
                pq->heads[p] = pq->tails[p] = NULL;
        }

        pq->ready_mask = pq->size = 0;
        pthread_mutex_unlock(&pq->mutex);
        pthread_cond_destroy(&pq->not_empty);
        pthread_mutex_destroy(&pq->mutex);
        return;
}

void pq_push(struct priority_queue_t *pq, struct task_t *task)
{
        if (!pq || !task)
                return;

        int p = (int)task->priority;
        pthread_mutex_lock(&pq->mutex);
        task->next = NULL;

        if (!pq->tails[p]) 
                pq->heads[p] = pq->tails[p] = task;
        else
                pq->tails[p] = pq->tails[p]->next = task;

        // mask ready task at p priority level
        pq->ready_mask |= (1UL << p);
        pq->size++;
        // wake up worker
        pthread_cond_signal(&pq->not_empty);
        pthread_mutex_unlock(&pq->mutex);
}

struct task_t *pq_pop(struct priority_queue_t *pq)
{
        return pq_pop_until_shutdown(pq, NULL);
}

struct task_t *pq_pop_until_shutdown(struct priority_queue_t *pq, const atomic_bool *shutdown_flag)
{
        if (!pq)
                return NULL;

        pthread_mutex_lock(&pq->mutex);

        // wait for task
        while (pq->ready_mask == 0) {
                if (_shutdown_requested(shutdown_flag)) {
                        pthread_mutex_unlock(&pq->mutex);
                        return NULL;
                }
                // go with unlock mutex and comeback with lock mutex
                pthread_cond_wait(&pq->not_empty, &pq->mutex); 
        }

        int p = _get_highest_priority(pq->ready_mask);
        struct task_t *task = pq->heads[p];
        pq->heads[p] = task->next;

        if (!pq->heads[p]) {
                pq->tails[p] = NULL;
                pq->ready_mask &= ~(1UL << p);
        }

        task->next = NULL;
        pq->size--;
        pthread_mutex_unlock(&pq->mutex);
        return task;
}

void pq_wake_all(struct priority_queue_t *pq)
{
  if (!pq)
    return;

  pthread_mutex_lock(&pq->mutex);
  pthread_cond_broadcast(&pq->not_empty);
  pthread_mutex_unlock(&pq->mutex);
}

struct task_t *pq_pop_nonblock(struct priority_queue_t *pq)
{
        if (!pq)
                return NULL;

        pthread_mutex_lock(&pq->mutex);

        // fast return
        if (pq->ready_mask == 0) {
                pthread_mutex_unlock(&pq->mutex);
                return NULL;
        }

        int p = _get_highest_priority(pq->ready_mask);
        struct task_t *task = pq->heads[p];
        pq->heads[p] = task->next;

        // task at priority level p is now empty
        if (!pq->heads[p]) {
                pq->tails[p] = NULL;
                pq->ready_mask &= ~(1UL << p);
        }

        task->next = NULL;
        pq->size--;
        pthread_mutex_unlock(&pq->mutex);
        return task;
}

int pq_size(struct priority_queue_t *pq)
{
        if (!pq)
                return 0;

        pthread_mutex_lock(&pq->mutex);
        int size = pq->size;
        pthread_mutex_unlock(&pq->mutex);
        return size;
}

int pq_is_empty(struct priority_queue_t *pq)
{
        if (!pq)
                return 0;

        pthread_mutex_lock(&pq->mutex);
        int empty = (pq->ready_mask == 0);
        pthread_mutex_unlock(&pq->mutex);
        return empty;
}
