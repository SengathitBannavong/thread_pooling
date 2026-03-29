#include "include/bench.h"
#include "include/work.h"
#include "../include/threadpool.h"
#include <unistd.h>
#include <stdatomic.h>

static inline void sleep_ms(int ms)
{
        struct timespec ts;
        ts.tv_sec = ms / 1000;
        ts.tv_nsec = (long)(ms % 1000) * 1000000L;
        nanosleep(&ts, NULL);
}

atomic_int count_task;
FILE* fp;

void fast_work(void *arg)
{
        (void)arg;
        fibonaccy(20);
        atomic_fetch_add_explicit(&count_task, -1, memory_order_release);
}

int main()
{
        int cycles = 20;
        int tasks_per_cycle = 500;
        thread_pool_t* pool = thread_pool_init(16);
        char buffer[256];
        snprintf(buffer, sizeof(buffer), "benchmark/res/result_stability.txt");
        
        system("mkdir -p benchmark/res");
        BENCH_CSV_INIT(fp, buffer);

        printf("--- Burst / Idle Stability Test ---\n");
        printf("Running %d cycles of %d tasks + 0.1s idle sleep...\n", cycles, tasks_per_cycle);

        for(int i = 0; i < cycles; i++) {
                atomic_store(&count_task, tasks_per_cycle);
                
                BENCH_START();
                for(int j = 0; j < tasks_per_cycle; j++) {
                        thread_pool_submit(pool, fast_work, NULL, PRIORITY_MEDIUM);
                }
                
                while(atomic_load_explicit(&count_task, memory_order_acquire)) {
                        // busy wait for tasks to finish
                }
                BENCH_STOP();
                BENCH_WRITE(fp, "burst", tasks_per_cycle, i+1);

                printf("[INFO] Cycle %d completed. Sleeping...\n", i+1);
                sleep_ms(100); // 0.1s idle period
        }

        printf("Stability test finished. Shutting down pool.\n");
        thread_pool_destroy(&pool);
        BENCH_CSV_CLOSE(fp);
        return 0;
}
