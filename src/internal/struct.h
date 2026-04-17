#ifndef _THREAD_POOL_STRUCT_H
#define _THREAD_POOL_STRUCT_H

#include <bits/pthreadtypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <pthread.h>
#include "priority_queue.h"
#include "worker.h"


struct monitor_t {
        pthread_t               tid;
        bool                    tid_valid;      /* true → thread exists, safe to join  */
        atomic_bool             running;        /* true → thread loop active (attached) */
        atomic_bool             ever_init;
        thread_pool_t*          pool;           /* pointer back to own pool */
};

struct thread_pool {
        struct worker_t         *workers;
        uint32_t                num_workers;
        struct priority_queue_t pq;             /* shared priority queue */
        atomic_int              total_task_in_system;
        atomic_bool             shutdown;       /* set true to begin shutdown */
        pthread_mutex_t         drain_mutex;    /* guards drain_cond */
        pthread_cond_t          drain_cond;     /* signalled when total_task_in_system == 0 */
        atomic_uint             in_flight_submits; /* ensure no one will destroy pool before submit */
        atomic_bool             paused;         /* set true to begin paused and only resume is set to false */
        pthread_mutex_t         pause_mutex;
        pthread_cond_t          pause_cond;     /* signalled when resume is call */
        pthread_t               aging_tid;
        pthread_mutex_t         aging_mutex;
        pthread_cond_t          aging_cond;
        atomic_bool             aging_enable;
        long                    aging_interval_ms;
        long                    aging_promote_ms;
        time_t                  start_time;
        struct monitor_t        monitor;
};

#endif
