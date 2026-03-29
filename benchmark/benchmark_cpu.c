#include "include/bench.h"
#include "include/work.h"
#include "../include/threadpool.h"
#include "../include/cpu_core.h"
#include <pthread.h>
#include <stdlib.h>

FILE* fp;

void work_core(void *arg) {
        (void) arg;
        fibonaccy(30);
}

void *work_pthread(void *arg) {
        work_core(arg);
        return NULL;
}

int thread_pool_bench(int n, int run)
{
        printf("[INFO] start thread pool\n");
        int core = get_num_core();
        thread_pool_t* pool = thread_pool_init(core);
        if (!pool) return -1;

        BENCH_START();

        for(int i = 0; i < n; i++)
                thread_pool_submit(pool, work_core, NULL, PRIORITY_HIGH);

        thread_pool_destroy(&pool);

        BENCH_STOP();
        BENCH_WRITE(fp, "thread_pool", n, run);
        return 0;
}


int on_demand_bench(int n, int run)
{
        printf("[INFO] start on demand\n");
        pthread_t *t = malloc(sizeof(pthread_t) * n);
        if (!t) return -1;

        BENCH_START();
        
        for(int i = 0; i < n; i++) {
                pthread_create(&t[i], NULL, work_pthread, NULL);
        }

        for(int i = 0; i < n; i++) {
                pthread_join(t[i], NULL);
        }

        BENCH_STOP();
        BENCH_WRITE(fp, "on_demand", n, run);
        free(t);
        return 0;
}

int sequential_bench(int n, int run, int last_run) 
{
        printf("[INFO] start sequential_bench\n");
        BENCH_START();
        for(int i = 0; i < n; i++) {
                work_core(NULL);
        }
        BENCH_STOP();
        BENCH_WRITE(fp, "sequential", n, run);
        // run only one time for base line
        BENCH_WRITE(fp, "sequential", n, last_run);
        return 0;
}


int main(int argc,char *argv[]) {
        if(argc < 3) {
                printf("Usage: %s <n_tasks> <n_runs>\n", argv[0]);
                return -1;
        }

        int n = atoi(argv[1]);
        int spin = atoi(argv[2]);
        char buffer[256];
        snprintf(buffer, sizeof(buffer), "benchmark/res/result_cpu_bound_n_%d.txt", n);
        
        system("mkdir -p benchmark/res");

        BENCH_CSV_INIT(fp, buffer);
        printf("[INFO] check your device have %d core of cpu\n", get_num_core());
        for(int i = 1; i <= spin; i++) {
                printf("[INFO] start spin #%d\n", i);
                // one time run
                if(i == 1) {
                        if(sequential_bench(n, i, spin))
                	        break;
                }             
                if(on_demand_bench(n, i))
                        break;
                
                if(thread_pool_bench(n, i))
                        break;
        }

        BENCH_CSV_CLOSE(fp);
        return 0;
}