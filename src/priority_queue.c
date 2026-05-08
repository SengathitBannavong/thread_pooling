#include "priority_queue.h"
#include "task.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>

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
        pthread_mutex_unlock(&pq->mutex);
        pthread_cond_signal(&pq->not_empty);
}

static void pq_push_head(struct priority_queue_t *pq, struct task_t *task)
{
        if (!pq || !task)
                return;

        int p = (int)task->priority;
        pthread_mutex_lock(&pq->mutex);
        task->next = NULL;

        if (!pq->tails[p])
                pq->heads[p] = pq->tails[p] = task;
        else {
                task->next = pq->heads[p];
                pq->heads[p] = task;
        }

        // mask ready task at p priority level
        pq->ready_mask |= (1UL << p);
        pq->size++;
        // wake up worker
        pthread_mutex_unlock(&pq->mutex);
        pthread_cond_signal(&pq->not_empty);
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

static inline bool has_passed_ms(struct timespec *start, struct timespec *end, long target_ms) {
        long sec_diff = end->tv_sec - start->tv_sec;
        long nsec_diff = end->tv_nsec - start->tv_nsec;

        if (nsec_diff < 0) {
                sec_diff -= 1;
                nsec_diff += 1000000000L;
        }

        long total_ns = sec_diff * 1000000000L + nsec_diff;
        return total_ns >= target_ms * 1000000L;
}

static struct task_t *pq_pop_until_shutdown_without_lock(struct priority_queue_t *pq, const atomic_bool *shutdown_flag,int priority)
{
        if (!pq)
                return NULL;

        if (_shutdown_requested(shutdown_flag)) {
                return NULL;
        }

        if(!((pq->ready_mask) & (1UL << priority)))
                return NULL;

        struct task_t *task = pq->heads[priority];
        pq->heads[priority] = task->next;

        if (!pq->heads[priority]) {
                pq->tails[priority] = NULL;
                pq->ready_mask &= ~(1UL << priority);
        }

        task->next = NULL;
        pq->size--;
        return task;
}

void pq_aging(struct priority_queue_t *pq, const atomic_bool *shutdown_flag, const long promote_time_ms)
{
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);

        /* MEDIUM -> HIGH */
        while (!_shutdown_requested(shutdown_flag)) {
                pthread_mutex_lock(&pq->mutex);

                struct task_t *head = pq->heads[PRIORITY_MEDIUM];
                if (!head || !has_passed_ms(&head->submit_time, &now, promote_time_ms)) {
                        pthread_mutex_unlock(&pq->mutex);
                        break;
                }

                struct task_t *task = pq_pop_until_shutdown_without_lock(pq, shutdown_flag, PRIORITY_MEDIUM);
                pthread_mutex_unlock(&pq->mutex);

                if (!task)
                        break;

                task->priority = PRIORITY_HIGH;
                task->next = NULL;
                pq_push_head(pq, task);
        }

        clock_gettime(CLOCK_MONOTONIC, &now);
        /* LOW -> MEDIUM */
        while (!_shutdown_requested(shutdown_flag)) {
                pthread_mutex_lock(&pq->mutex);

                struct task_t *head = pq->heads[PRIORITY_LOW];
                if (!head || !has_passed_ms(&head->submit_time, &now, promote_time_ms)) {
                        pthread_mutex_unlock(&pq->mutex);
                        break;
                }

                struct task_t *task = pq_pop_until_shutdown_without_lock(pq, shutdown_flag, PRIORITY_LOW);
                pthread_mutex_unlock(&pq->mutex);

                if (!task)
                        break;

                task->priority = PRIORITY_MEDIUM;
                task->next = NULL;
                pq_push_head(pq, task);
        }
}
