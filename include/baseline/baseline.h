#ifndef BASELINE_H
#define BASELINE_H

#include <stdatomic.h>
#include <ctype.h>
#include <pthread.h>
#include <stdint.h>

typedef struct baseline_pool_t baseline_pool_t;

baseline_pool_t* baseline_pool_init(uint32_t num_workers);

int64_t baseline_pool_submit(baseline_pool_t *pool, void (*task_fun_t)(void *arg), void *arg);

void baseline_pool_wait(baseline_pool_t *pool);

void baseline_pool_shutdown(baseline_pool_t **pool);

#endif // BASELINE_H
