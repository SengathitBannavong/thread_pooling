#include "worker.h"
#include <stdatomic.h>
#include <stdint.h>

uint16_t worker_capacity_busy(struct worker_t *workers, uint16_t num_of_worker)
{
        uint16_t count = 0;
        for(int i = 0; i < num_of_worker; i++) {
                if(atomic_load_explicit(&workers[i].busy, memory_order_acquire))
                        count += 1;
        }
        return count;
}
