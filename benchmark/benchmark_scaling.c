#include "include/bench.h"
#include "include/work.h"
#include "../include/threadpool.h"

FILE* fp;

void work_core(void *arg) {
        (void)arg;
        fibonaccy(25); // Medium task
}

int main() {
        int tasks = 10000;
        int worker_counts[] = {1, 2, 4, 8, 16, 32, 64};
        int n_counts = sizeof(worker_counts) / sizeof(int);
        char buffer[256];
        snprintf(buffer, sizeof(buffer), "benchmark/res/result_scaling.txt");
        
        system("mkdir -p benchmark/res");
        BENCH_CSV_INIT(fp, buffer);

        printf("--- Worker Scaling Test (10,000 tasks) ---\n");
        printf("Workers | Time (ms) | Throughput (Tasks/sec)\n");
        printf("---------------------------------------------\n");

        for(int i = 0; i < n_counts; i++) {
                int w = worker_counts[i];
                thread_pool_t* pool = thread_pool_init(w);
                
                BENCH_START();
                for(int j = 0; j < tasks; j++) 
                        thread_pool_submit(pool, work_core, NULL, PRIORITY_MEDIUM);
                
                thread_pool_destroy(&pool);
                BENCH_STOP();
                
                char method_name[32];
                snprintf(method_name, sizeof(method_name), "%d_workers", w);
                BENCH_WRITE(fp, method_name, tasks, 1);

                double total_ms = (_t1 - _t0) / 1000.0;
                double throughput = tasks * 1e6 / (_t1 - _t0);
                printf("%7d | %9.2f | %9.2f\n", w, total_ms, throughput);
        }

        BENCH_CSV_CLOSE(fp);
        return 0;
}
