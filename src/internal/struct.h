#ifndef _THREAD_POOL_STRUCT_H
#define _THREAD_POOL_STRUCT_H

#include <stdint.h>
#include <stdatomic.h>
#include <pthread.h>
#include "priority_queue.h"
#include "worker.h"


struct thread_pool {
        struct worker_t         *workers;
        /* total workers thread */
        uint32_t                num_workers;
        /* shared priority queue */
        struct priority_queue_t pq;
        /* tasks currently executing */
        atomic_int              total_task_in_system;
        /* set true to begin shutdown */
        atomic_bool             shutdown;
        /* guards drain_cond */
        pthread_mutex_t         drain_mutex;
        /* signalled when total_task_in_system == 0 */
        pthread_cond_t          drain_cond;
        /* ensure no one will destroy pool before submit */
        atomic_uint             in_flight_submits;
        /* set true to begin paused and only resume is set to false */
        atomic_bool             paused;
        /* signalled when resume is call */
        pthread_cond_t          pause_cond;
};

#endif
