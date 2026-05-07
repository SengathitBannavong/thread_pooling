/**
 *
 * Baseline thread pool — minimalist FIFO design used for comparative
 * evaluation only. Structure adapted from:
 *
 *   Pacheco, P. S., & Malensek, M. (2021).
 *   "An Introduction to Parallel Programming" (2nd ed.), Section 4.8.
 *   Morgan Kaufmann.
 *
 * Differences from textbook:
 *   - Added drain_cond + total_task_in_system for graceful shutdown
 *     parity with the main project pool (so destroy() guarantees
 *     no task is dropped, matching main pool semantics).
 *   - Otherwise minimalist: single FIFO, single queue mutex.
 *
 * Baseline thread pool implementation for benchmarking.
 *
 * This implementation is used to compare performance against the
 * custom thread pool. It lacks advanced features such as:
 *   - Priority queue
 *   - Pause/resume capabilities
 *   - Task monitoring
 *   - Task aging
 *
 * The primary goal is to observe the performance trade-offs and
 * measure the overhead introduced by these additional features.
 */
#include "baseline.h"
#include <bits/pthreadtypes.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

struct baseline_task_t {
        uint16_t                task_id;
        void                    (*func)(void*);
        void*                   arg;
        struct baseline_task_t  *next;
};


struct baseline_queue_t {
        struct baseline_task_t *head;
        struct baseline_task_t *tail;
        pthread_mutex_t         q_mutex;
        pthread_cond_t          q_cond;
};

struct baseline_pool_t {
        pthread_t*              worker_thread;
        uint32_t                num_workers;
        atomic_bool             shutdown;
        pthread_mutex_t         drain_mutex;
        pthread_cond_t          drain_cond;
        atomic_int              in_flight_submits;
        atomic_int              total_task_in_system;
        struct baseline_queue_t queue;
};


int32_t q_push(struct baseline_queue_t *q, struct baseline_task_t *task)
{
        pthread_mutex_lock(&q->q_mutex);
        if (q->tail) {
                q->tail->next = task;
        } else {
                q->head = task;
        }
        q->tail = task;
        pthread_mutex_unlock(&q->q_mutex);
        pthread_cond_signal(&q->q_cond);
        return 0;
}

struct baseline_task_t *q_pop_until_shutdown(struct baseline_queue_t *q, const atomic_bool *shutdown_flag)
{
        struct baseline_task_t *task = NULL;
        pthread_mutex_lock(&q->q_mutex);

        while (!q->head) {
                if(atomic_load_explicit(shutdown_flag, memory_order_acquire)) {
                        pthread_mutex_unlock(&q->q_mutex);
                        return NULL;
                }

                pthread_cond_wait(&q->q_cond, &q->q_mutex);
        }
        task = q->head;
        q->head = q->head->next;
        if (!q->head) {
                q->tail = NULL;
        }

        pthread_mutex_unlock(&q->q_mutex);
        return task;
}

int q_wake_all(struct baseline_queue_t *q)
{
        pthread_mutex_lock(&q->q_mutex);
        pthread_cond_broadcast(&q->q_cond);
        pthread_mutex_unlock(&q->q_mutex);
        return 0;
}

static void *worker_func(void *arg)
{
        baseline_pool_t *pool = (baseline_pool_t *)arg;
        while (1) {
                struct baseline_task_t *task = q_pop_until_shutdown(&pool->queue, &pool->shutdown);
                if (!task)
                        break;
                task->func(task->arg);
                free(task);
                int32_t remain = atomic_fetch_add_explicit(&pool->total_task_in_system, -1, memory_order_release) -1;
                if (remain <= 0) {
                        pthread_mutex_lock(&pool->drain_mutex);
                        pthread_cond_broadcast(&pool->drain_cond);
                        pthread_mutex_unlock(&pool->drain_mutex);
                }
        }
        return NULL;
}


baseline_pool_t* baseline_pool_init(uint32_t num_workers)
{

        if (num_workers <= 0)
                return NULL;

        baseline_pool_t* pool = malloc(sizeof(baseline_pool_t));
        if (!pool)
                return NULL;

        pool->worker_thread = malloc(sizeof(pthread_t) * num_workers);
        if (!pool->worker_thread)
                goto error_pool;

        pool->num_workers = num_workers;
        atomic_store_explicit(&pool->shutdown, false, memory_order_relaxed);
        atomic_store_explicit(&pool->in_flight_submits, 0, memory_order_relaxed);
        atomic_store_explicit(&pool->total_task_in_system, 0, memory_order_relaxed);
        if(pthread_mutex_init(&pool->drain_mutex, NULL) != 0)
                goto error_worker;

        if(pthread_cond_init(&pool->drain_cond, NULL) != 0)
                goto error_mutex;

        pool->queue.head = pool->queue.tail = NULL;

        if(pthread_mutex_init(&pool->queue.q_mutex, NULL) != 0)
                goto error_cond;

        if(pthread_cond_init(&pool->queue.q_cond, NULL) != 0)
                goto error_mutex_q;

        for(uint32_t i = 0 ; i < num_workers; i++) {
                if(pthread_create(&pool->worker_thread[i], NULL, worker_func, (void *)pool)) {
                        atomic_store_explicit(&pool->shutdown, true, memory_order_relaxed);
                        q_wake_all(&pool->queue);
                        for(uint32_t j = 0; j < i; j++) {
                                pthread_join(pool->worker_thread[j], NULL);
                        }
                        goto error_cond_q;
                }
        }

        return pool;

        error_cond_q:pthread_cond_destroy(&pool->queue.q_cond);
        error_mutex_q:pthread_mutex_destroy(&pool->queue.q_mutex);
        error_cond:pthread_cond_destroy(&pool->drain_cond);
        error_mutex:pthread_mutex_destroy(&pool->drain_mutex);
        error_worker:free(pool->worker_thread);
        error_pool:free(pool);
        return NULL;
}

int64_t baseline_pool_submit(baseline_pool_t *pool, void (*task_fun_t)(void *arg), void *arg)
{
        if (!pool || !task_fun_t)
                return -1;
        if (atomic_load_explicit(&pool->shutdown, memory_order_acquire))
                return -1;

        atomic_fetch_add_explicit(&pool->in_flight_submits, 1, memory_order_release);

        struct baseline_task_t *task = malloc(sizeof(struct baseline_task_t));
        if (!task) {
                atomic_fetch_add_explicit(&pool->in_flight_submits, -1, memory_order_release);
                return -1;
        }

        task->func = task_fun_t;
        task->arg = arg;
        task->next = NULL;

        atomic_fetch_add_explicit(&pool->total_task_in_system, 1, memory_order_release);
        q_push(&pool->queue, task);
        atomic_fetch_add_explicit(&pool->in_flight_submits, -1, memory_order_release);
        return 0;
}

void baseline_pool_wait(baseline_pool_t *pool) {
        pthread_mutex_lock(&pool->drain_mutex);
        while (atomic_load_explicit(&pool->total_task_in_system, memory_order_acquire))
                pthread_cond_wait(&pool->drain_cond, &pool->drain_mutex);
        pthread_mutex_unlock(&pool->drain_mutex);
}

void baseline_pool_shutdown(baseline_pool_t **pool)
{
        if (!pool || !*pool)
                return;

        atomic_store_explicit(&(*pool)->shutdown, true, memory_order_relaxed);
        q_wake_all(&(*pool)->queue);
        // block until all in-flight submits are processed
        while (atomic_load_explicit(&(*pool)->in_flight_submits, memory_order_acquire));

        pthread_mutex_lock(&(*pool)->drain_mutex);

        while (atomic_load_explicit(&(*pool)->total_task_in_system, memory_order_acquire))
                pthread_cond_wait(&(*pool)->drain_cond, &(*pool)->drain_mutex);

        pthread_mutex_unlock(&(*pool)->drain_mutex);

        for (uint32_t i = 0; i < (*pool)->num_workers; i++)
                pthread_join((*pool)->worker_thread[i], NULL);

        pthread_cond_destroy(&(*pool)->queue.q_cond);
        pthread_mutex_destroy(&(*pool)->queue.q_mutex);
        pthread_cond_destroy(&(*pool)->drain_cond);
        pthread_mutex_destroy(&(*pool)->drain_mutex);
        free((*pool)->worker_thread);
        free(*pool);
        *pool = NULL;
}
