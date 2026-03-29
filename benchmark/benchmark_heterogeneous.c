#include "include/bench.h"
#include "include/work.h"
#include "../include/threadpool.h"

FILE* fp;

void heavy_work(void *arg) {
        (void)arg;
        fibonaccy(40);
}

void tiny_work(void *arg) {
        (void)arg;
        fibonaccy(10);
}

int main() {
        int core = get_num_core();
        thread_pool_t* pool = thread_pool_init(core);
        char buffer[256];
        snprintf(buffer, sizeof(buffer), "benchmark/res/result_hetero.txt");
        
        system("mkdir -p benchmark/res");
        BENCH_CSV_INIT(fp, buffer);

        printf("--- Heterogeneous Workload Test ---\n");
        printf("Mixing 100 heavy tasks (Fib 40) with 10,000 tiny tasks (Fib 10)...\n");

        BENCH_START();
        
        // Inject mixed work
        int heavy_n = 100;
        int tiny_n = 100;
        for(int i = 0; i < heavy_n; i++) {
                thread_pool_submit(pool, heavy_work, NULL, PRIORITY_MEDIUM);
                for(int j = 0; j < tiny_n; j++) {
                        thread_pool_submit(pool, tiny_work, NULL, PRIORITY_MEDIUM);
                }
        }

        thread_pool_destroy(&pool);
        BENCH_STOP();
        
        int total_tasks = heavy_n + heavy_n * tiny_n;
        BENCH_WRITE(fp, "heterogeneous", total_tasks, 1);

        printf("Total time: %.2f ms\n", (_t1 - _t0) / 1000.0);
        printf("Result: Pool successfully completed mixed heavy/light load.\n");
        
        BENCH_CSV_CLOSE(fp);
        return 0;
}
