#ifndef _WORKER_H
#define _WORKER_H

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>

typedef struct thread_pool thread_pool_t;

struct worker_t {
        pthread_t       tid;            /* thread id */
        uint16_t        id;             /* id of worker */
        int16_t         core_id;        /* future pinning core */
        atomic_bool     busy;
        thread_pool_t*  pool;           /* back pointer */
};

uint16_t worker_capacity_busy(struct worker_t *workers, uint16_t num_of_worker);

#endif
