#ifndef MONITOR_H
#define MONITOR_H

#include <stdbool.h>
#include "thread_pool.h"

void monitor_start(thread_pool_t *pool);

void monitor_stop(thread_pool_t *pool);

int monitor_detach(thread_pool_t *pool);

int monitor_reattach(thread_pool_t *pool);

bool monitor_attached(thread_pool_t *pool);

#endif /* MONITOR_H */
