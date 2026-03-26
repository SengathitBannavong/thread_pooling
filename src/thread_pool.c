#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include "thread_pool.h"

struct thread_pool {
        pthread_t *workers; /* array[num_workers] of thread ids */
        uint32_t num_workers; /* total workers thread */
        struct priority_queue_t pq; /* shared priority queue */
        atomic_int total_task_in_system; /* tasks currently executing */
        atomic_bool shutdown; /* set true to begin shutdown */
        pthread_mutex_t drain_mutex; /* guards drain_cond */
        pthread_cond_t drain_cond; /* signalled when total_task_in_system == 0 */
        atomic_uint in_flight_submits; /* ensure no one will destroy pool before submit */
};

/* --- worker schedule --- */
static void *worker_func(void *arg)
{
        thread_pool_t *pool = (thread_pool_t *)arg;

        for (;;) {
                struct task_t *task = pq_pop_until_shutdown(&pool->pq, &pool->shutdown);

                /* NULL -> time to exit */
                if (!task) 
                        break;

                task->func(task->arg);
                task_destroy(task);

                int remaining = atomic_fetch_add_explicit(&pool->total_task_in_system, -1, memory_order_acq_rel) - 1;

                /* if we just finished the last active task, wake pool_destroy() */
                if (remaining == 0) {
                        pthread_mutex_lock(&pool->drain_mutex);
                        pthread_cond_signal(&pool->drain_cond);
                        pthread_mutex_unlock(&pool->drain_mutex);
                }
        }

        return NULL;
}

thread_pool_t *thread_pool_init(int num_workers)
{
        if (num_workers <= 0) {
                errno = EINVAL;
                return NULL;
        }

        thread_pool_t *pool = malloc(sizeof(struct thread_pool));
        if (!pool) {
                return NULL;
        }

        pool->num_workers = num_workers;
        atomic_store_explicit(&pool->in_flight_submits, 0, memory_order_relaxed);
        atomic_store_explicit(&pool->total_task_in_system, 0, memory_order_relaxed);
        atomic_store_explicit(&pool->shutdown, false, memory_order_relaxed);

        if (pq_init(&pool->pq) != 0) {
                free(pool);
                return NULL;
        }
        
        if (pthread_mutex_init(&pool->drain_mutex, NULL) != 0) {
                pq_destroy(&pool->pq);
                free(pool);
                return NULL;
        }

        if (pthread_cond_init(&pool->drain_cond, NULL) != 0) {
                pthread_mutex_destroy(&pool->drain_mutex);
                pq_destroy(&pool->pq);
                free(pool);
                return NULL;
        }

        /* worker allocate */
        pool->workers = malloc((size_t)num_workers * sizeof(pthread_t));
        if (!pool->workers) {
                pthread_cond_destroy(&pool->drain_cond);
                pthread_mutex_destroy(&pool->drain_mutex);
                pq_destroy(&pool->pq);
                free(pool);
                return NULL;
        }

        /* spawn workers */
        for (int i = 0; i < num_workers; i++) {
                if (pthread_create(&pool->workers[i], NULL, worker_func, pool) != 0) {
                        /* mark shutdown for another already-running workers will stop */
                        atomic_store_explicit(&pool->shutdown, true, memory_order_release);
                        pq_wake_all(&pool->pq);

                        for (int j = 0; j < i; j++)
                                pthread_join(pool->workers[j], NULL);

                        pq_destroy(&pool->pq);
                        free(pool->workers);
                        pthread_cond_destroy(&pool->drain_cond);
                        pthread_mutex_destroy(&pool->drain_mutex);
                        free(pool);
                        return NULL;
                }
        }
        return pool;
}

int64_t thread_pool_submit(thread_pool_t *pool, void (*task_fun_t)(void *arg),
                void *arg, enum task_priority priority)
{
        if (!pool || !task_fun_t) 
                return -1;
        
        atomic_fetch_add_explicit(&pool->in_flight_submits, 1, memory_order_release);

        /* reject new work once shutdown has begun */
        if (atomic_load_explicit(&pool->shutdown, memory_order_acquire)) {
                atomic_fetch_add_explicit(&pool->in_flight_submits, -1, memory_order_release);
                return -1;
        }
        
        struct task_t *task = task_create(task_fun_t, arg, priority);

        if (!task) {
                atomic_fetch_add_explicit(&pool->in_flight_submits, -1, memory_order_release);
                return -1;
        }

        int64_t id = (int64_t)task->task_id;
        atomic_fetch_add_explicit(&pool->total_task_in_system, 1, memory_order_relaxed);
        pq_push(&pool->pq, task);
        atomic_fetch_add_explicit(&pool->in_flight_submits, -1, memory_order_release);
        return id;
}

void thread_pool_destroy(thread_pool_t **ori_pool)
{
        if (!ori_pool || !*ori_pool) 
                return;
        
        thread_pool_t *pool = *ori_pool;
        atomic_store_explicit(&pool->shutdown, true, memory_order_release);

        /* ensure workers blocked in pq_pop_until_shutdown wake up and exit */
        pq_wake_all(&pool->pq);
        pthread_mutex_lock(&pool->drain_mutex);

        while (atomic_load_explicit(&pool->total_task_in_system, memory_order_acquire) > 0)
                pthread_cond_wait(&pool->drain_cond, &pool->drain_mutex);

        pthread_mutex_unlock(&pool->drain_mutex);

        while(atomic_load_explicit(&pool->in_flight_submits, memory_order_acquire) > 0); // block

        for (uint32_t i = 0; i < pool->num_workers; i++) 
                pthread_join(pool->workers[i], NULL);

        pq_destroy(&pool->pq);
        free(pool->workers);
        pool->workers = NULL;

        pthread_cond_destroy(&pool->drain_cond);
        pthread_mutex_destroy(&pool->drain_mutex);
        free(pool);
        *ori_pool = NULL;
}
