#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include "thread_pool.h"
#include "monitor.h"
#include "priority_queue.h"
#include "worker.h"
#include "internal/struct.h"

/* --- worker schedule --- */
static void *worker_func(void *arg)
{
        struct worker_t *worker = (struct worker_t *)arg;
        thread_pool_t *pool = worker->pool;

        for (;;) {

                if(atomic_load_explicit(&pool->paused, memory_order_acquire)) {
                        pthread_mutex_lock(&pool->pause_mutex);

                        while(atomic_load_explicit(&pool->paused, memory_order_acquire))
                                pthread_cond_wait(&pool->pause_cond, &pool->pause_mutex);

                        pthread_mutex_unlock(&pool->pause_mutex);
                }

                struct task_t *task = pq_pop_until_shutdown(&pool->pq, &pool->shutdown);

                /* NULL -> time to exit */
                if (!task)
                        break;

                atomic_store_explicit(&worker->busy, true, memory_order_release);
                task->func(task->arg);
                task_destroy(task);
                atomic_store_explicit(&worker->busy, false, memory_order_release);

                int remaining = atomic_fetch_add_explicit(&pool->total_task_in_system, -1, memory_order_acq_rel) - 1;

                /* if we just finished the last active task, wake pool_destroy() or pool_wait() */
                if (remaining == 0) {
                        pthread_mutex_lock(&pool->drain_mutex);
                        pthread_cond_signal(&pool->drain_cond);
                        pthread_mutex_unlock(&pool->drain_mutex);
                }
        }

        return NULL;
}

static inline void timespec_add_ms(struct timespec *ts, long ms)
{
        if (ms <= 0)
                return;

        ts->tv_sec += ms / 1000;
        ts->tv_nsec += (ms % 1000) * 1000000L;

        if (ts->tv_nsec >= 1000000000L) {
                ts->tv_sec += ts->tv_nsec / 1000000000L;
                ts->tv_nsec %= 1000000000L;
        }
}

static void *aging_worker(void *arg)
{
        thread_pool_t *pool = (thread_pool_t *)arg;
        const long interval_ms = pool->aging_interval_ms;

        pthread_mutex_lock(&pool->aging_mutex);
        while (1) {
                if (atomic_load_explicit(&pool->paused, memory_order_acquire)) {
                        pthread_mutex_unlock(&pool->aging_mutex);
                        pthread_mutex_lock(&pool->pause_mutex);

                        while (atomic_load_explicit(&pool->paused, memory_order_acquire)) {

                                if(atomic_load_explicit(&pool->shutdown, memory_order_acquire)) {
                                        break;
                                }

                                if(!atomic_load_explicit(&pool->aging_enable, memory_order_acquire)) {
                                        break;
                                }

                                pthread_cond_wait(&pool->pause_cond, &pool->pause_mutex);
                        }
                        pthread_mutex_unlock(&pool->pause_mutex);
                        pthread_mutex_lock(&pool->aging_mutex);
                }

                if (atomic_load_explicit(&pool->shutdown, memory_order_acquire))
                        break;

                if (!atomic_load_explicit(&pool->aging_enable, memory_order_acquire))
                        break;

                /* Compute absolute wake time */
                struct timespec ts;
                clock_gettime(CLOCK_MONOTONIC, &ts);
                timespec_add_ms(&ts, interval_ms);

                pthread_cond_timedwait(&pool->aging_cond, &pool->aging_mutex, &ts);

                pq_aging(&pool->pq, &pool->shutdown, pool->aging_promote_ms);
        }
        pthread_mutex_unlock(&pool->aging_mutex);

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
        atomic_store_explicit(&pool->aging_enable, false, memory_order_relaxed);
        pool->aging_interval_ms = 0;
        pool->aging_promote_ms = 0;
        atomic_store_explicit(&pool->in_flight_submits, 0, memory_order_relaxed);
        atomic_store_explicit(&pool->total_task_in_system, 0, memory_order_relaxed);
        atomic_store_explicit(&pool->shutdown, false, memory_order_relaxed);
        atomic_store_explicit(&pool->paused, false, memory_order_relaxed);

        if (pq_init(&pool->pq) != 0)
                goto err_free_pool;

        if (pthread_mutex_init(&pool->drain_mutex, NULL) != 0)
                goto err_pq;

        if (pthread_cond_init(&pool->drain_cond, NULL) != 0)
                goto err_drain_mutex;

        if (pthread_mutex_init(&pool->aging_mutex, NULL) != 0)
                goto err_drain_cond;

        pthread_condattr_t aging_cond_attr;
        if (pthread_condattr_init(&aging_cond_attr) != 0)
                goto err_aging_mutex;

        if (pthread_condattr_setclock(&aging_cond_attr, CLOCK_MONOTONIC) != 0) {
                pthread_condattr_destroy(&aging_cond_attr);
                goto err_aging_mutex;
        }

        if (pthread_cond_init(&pool->aging_cond, &aging_cond_attr) != 0) {
                pthread_condattr_destroy(&aging_cond_attr);
                goto err_aging_mutex;
        }
        pthread_condattr_destroy(&aging_cond_attr);

        if (pthread_mutex_init(&pool->pause_mutex, NULL) != 0)
                goto err_aging_cond;

        if (pthread_cond_init(&pool->pause_cond, NULL) != 0)
                goto err_pause_mutex;

        /* worker allocate */
        pool->workers = malloc((size_t)num_workers * sizeof(struct worker_t));
        if (!pool->workers)
                goto err_pause_cond;

        /* spawn workers */
        for (int i = 0; i < num_workers; i++) {
                pool->workers[i].id = i;
                pool->workers[i].core_id = -1; // maybe using on future
                atomic_store_explicit(&pool->workers[i].busy, false, memory_order_relaxed);
                pool->workers[i].pool = pool;

                if (pthread_create(&pool->workers[i].tid, NULL, worker_func, &pool->workers[i]) != 0) {
                        /* mark shutdown so already-running workers will stop */
                        atomic_store_explicit(&pool->shutdown, true, memory_order_release);
                        pq_wake_all(&pool->pq);

                        for (int j = 0; j < i; j++)
                                pthread_join(pool->workers[j].tid, NULL);

                        goto err_workers;
                }
        }

        pool->start_time = time(NULL);

        /* monitor init */
        atomic_store_explicit(&pool->monitor.running, false, memory_order_release);
        atomic_store_explicit(&pool->monitor.ever_init, false, memory_order_release);
        pool->monitor.tid_valid     = false;
        pool->monitor.pool          = pool;

        return pool;

err_workers:        free(pool->workers);
err_pause_cond:     pthread_cond_destroy(&pool->pause_cond);
err_pause_mutex:    pthread_mutex_destroy(&pool->pause_mutex);
err_aging_cond:     pthread_cond_destroy(&pool->aging_cond);
err_aging_mutex:    pthread_mutex_destroy(&pool->aging_mutex);
err_drain_cond:     pthread_cond_destroy(&pool->drain_cond);
err_drain_mutex:    pthread_mutex_destroy(&pool->drain_mutex);
err_pq:             pq_destroy(&pool->pq);
err_free_pool:      free(pool);
                    return NULL;
}

int64_t thread_pool_submit(thread_pool_t *pool, void (*task_fun_t)(void *arg),
                void *arg, enum task_priority priority)
{
        if (!pool || !task_fun_t)
                return -1;

        atomic_fetch_add_explicit(&pool->in_flight_submits, 1, memory_order_seq_cst);

        // fast return if shutdown
        if (atomic_load_explicit(&pool->shutdown, memory_order_seq_cst)) {
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

        if (atomic_load_explicit(&pool->aging_enable, memory_order_acquire))
                clock_gettime(CLOCK_MONOTONIC, &task->submit_time);

        pq_push(&pool->pq, task);
        atomic_fetch_add_explicit(&pool->in_flight_submits, -1, memory_order_release);
        return id;
}

void thread_pool_pause(thread_pool_t *ori_pool)
{
        if (!ori_pool)
                return;

        atomic_store_explicit(&ori_pool->paused, true, memory_order_release);
        pq_wake_all(&ori_pool->pq);
}

static void thread_pool_wake_pause(thread_pool_t *ori_pool)
{
        pthread_mutex_lock(&ori_pool->pause_mutex);
        pthread_cond_broadcast(&ori_pool->pause_cond);
        pthread_mutex_unlock(&ori_pool->pause_mutex);
}

void thread_pool_resume(thread_pool_t *ori_pool)
{
        if (!ori_pool)
                return;

        atomic_store_explicit(&ori_pool->paused, false, memory_order_release);
        thread_pool_wake_pause(ori_pool);
}

void thread_pool_wait(thread_pool_t *ori_pool)
{
        if (!ori_pool)
                return;

        if(atomic_load_explicit(&ori_pool->paused, memory_order_acquire))
                thread_pool_resume(ori_pool);

        pthread_mutex_lock(&ori_pool->drain_mutex);

        while(atomic_load_explicit(&ori_pool->total_task_in_system, memory_order_acquire) > 0)
                pthread_cond_wait(&ori_pool->drain_cond, &ori_pool->drain_mutex);

        pthread_mutex_unlock(&ori_pool->drain_mutex);
        return;
}

uint16_t thread_pool_num_working(thread_pool_t *ori_pool) {
        if (!ori_pool)
                return 0;

        return worker_capacity_busy(ori_pool->workers, ori_pool->num_workers);
}

uint64_t thread_pool_queue_depth(thread_pool_t *pool)
{
        if (!pool)
                return 0;

        int total = atomic_load_explicit(&pool->total_task_in_system, memory_order_relaxed);
        uint16_t busy = worker_capacity_busy(pool->workers, pool->num_workers);

        if (total <= (int)busy)
                return 0;
        return (uint64_t)(total - (int)busy);
}

void thread_pool_monitor_start(thread_pool_t *pool)
{
        monitor_start(pool);
}

int thread_pool_monitor_detach(thread_pool_t *pool)
{
        return monitor_detach(pool);
}

int thread_pool_monitor_reattach(thread_pool_t *pool)
{
        return monitor_reattach(pool);
}

bool thread_pool_monitor_attached(thread_pool_t *pool)
{
        return monitor_attached(pool);
}

void thread_pool_destroy(thread_pool_t **ori_pool)
{
        if (!ori_pool || !*ori_pool)
                return;

        thread_pool_t *pool = *ori_pool;
        atomic_store_explicit(&pool->shutdown, true, memory_order_seq_cst);
        atomic_store_explicit(&pool->paused, false, memory_order_release);

        // ensure no thread in middle submits task
        while(atomic_load_explicit(&pool->in_flight_submits, memory_order_seq_cst) > 0); // block

        /* ensure workers blocked in pq_pop_until_shutdown wake up and exit */
        pq_wake_all(&pool->pq);
        thread_pool_wake_pause(pool);
        // wake up aging and join
        thread_pool_disable_aging(pool);

        pthread_mutex_lock(&pool->drain_mutex);

        while (atomic_load_explicit(&pool->total_task_in_system, memory_order_acquire) > 0)
                pthread_cond_wait(&pool->drain_cond, &pool->drain_mutex);

        pthread_mutex_unlock(&pool->drain_mutex);
        /* stop monitor BEFORE freeing workers – draw_ui reads pool->workers */
        monitor_stop(pool);

        for (uint32_t i = 0; i < pool->num_workers; i++)
                pthread_join(pool->workers[i].tid, NULL);

        pq_destroy(&pool->pq);
        free(pool->workers);
        pool->workers = NULL;

        pthread_cond_destroy(&pool->drain_cond);
        pthread_cond_destroy(&pool->pause_cond);
        pthread_cond_destroy(&pool->aging_cond);
        pthread_mutex_destroy(&pool->drain_mutex);
        pthread_mutex_destroy(&pool->pause_mutex);
        pthread_mutex_destroy(&pool->aging_mutex);
        free(pool);
        *ori_pool = NULL;
}


bool thread_pool_enable_aging(thread_pool_t *pool, const long interval_time_ms, const long promote_time_ms)
{
        if (!pool || interval_time_ms <= 0 || promote_time_ms <= 0)
                return false;

        pthread_mutex_lock(&pool->aging_mutex);
        if (atomic_load_explicit(&pool->aging_enable, memory_order_acquire)) {
                pthread_mutex_unlock(&pool->aging_mutex);
                return true;
        }
        pool->aging_interval_ms = interval_time_ms;
        pool->aging_promote_ms = promote_time_ms;
        atomic_store_explicit(&pool->aging_enable, true, memory_order_release);
        pthread_mutex_unlock(&pool->aging_mutex);

        if (pthread_create(&pool->aging_tid, NULL, aging_worker, pool) != 0) {
                atomic_store_explicit(&pool->aging_enable, false, memory_order_release);
                return false;
        }

        return true;
}

bool thread_pool_disable_aging(thread_pool_t *pool)
{
        if (!pool)
                return false;

        if (!atomic_load_explicit(&pool->aging_enable, memory_order_acquire))
                return true;

        atomic_store_explicit(&pool->aging_enable, false, memory_order_release);
        pthread_mutex_lock(&pool->aging_mutex);
        pthread_cond_signal(&pool->aging_cond);
        pthread_mutex_unlock(&pool->aging_mutex);
        thread_pool_wake_pause(pool);
        pthread_join(pool->aging_tid, NULL);

        return true;
}
