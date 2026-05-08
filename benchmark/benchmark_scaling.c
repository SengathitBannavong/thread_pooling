#define _POSIX_C_SOURCE 199309L
#include "include/bench.h"
#include "include/work.h"
#include "../include/threadpool.h"

FILE *fp;

static void work_core(void *arg) { (void)arg; fibonaccy(25); }

typedef struct { int n_tasks; int n_workers; } scale_arg_t;

static void run_scaling(void *_arg, bench_result_t *r)
{
        scale_arg_t *a = _arg;

        BENCH_MEM_BASELINE();
        thread_pool_t *pool = thread_pool_init(a->n_workers);
        if (!pool) return;
        BENCH_MEM_AFTER_INIT();

        double t0 = NOW_US();
        for (int j = 0; j < a->n_tasks; j++)
                thread_pool_submit(pool, work_core, NULL, PRIORITY_MEDIUM);
        thread_pool_wait(pool);
        BENCH_MEM_AFTER_RUN();
        thread_pool_destroy(&pool);
        double t1 = NOW_US();
        BENCH_MEM_AFTER_DESTROY();

        BENCH_RESULT_FILL(r, t0, t1);
}

int main(int argc, char *argv[])
{
        int tasks = 10000;
        int n_runs = (argc >= 2) ? atoi(argv[1]) : 1;
        int worker_counts[] = {1, 2, 4, 8, 16, 32, 64};
        int n_counts = (int)(sizeof(worker_counts) / sizeof(int));

        system("mkdir -p benchmark/res");
        fp = fopen("benchmark/res/result_scaling.txt", "w");
        if (!fp) { perror("fopen"); return 1; }
        /* mem_pool_cost_kb = mem_run - mem_base
         * = total RSS overhead from clean baseline to after all tasks are done.
         * By that point every worker has run tasks and faulted in its stack,
         * so this captures both pool overhead (stacks + struct) and any heap
         * growth from task allocation. Grows monotonically with worker count. */
        fprintf(fp, "method,n_tasks,run,"
                    "total_time_us,throughput,avg_latency_us,"
                    "mem_pool_cost_kb\n");

        for (int run = 1; run <= n_runs; run++) {
                printf("--- Worker Scaling Test (10,000 tasks) run %d/%d ---\n", run, n_runs);
                printf("%-8s | %-10s | %-22s | %-14s\n",
                       "Workers", "Time (ms)", "Throughput (tasks/s)", "total cost KB");
                printf("--------------------------------------------------------------\n");

                int ok = 1;
                for (int i = 0; i < n_counts; i++) {
                        int w = worker_counts[i];
                        scale_arg_t arg = { .n_tasks = tasks, .n_workers = w };
                        bench_result_t r;

                        if (bench_fork(run_scaling, &arg, &r) < 0) {
                                fprintf(stderr, "[ERROR] fork failed for %d workers\n", w);
                                ok = 0;
                                break;
                        }

                        char method[32];
                        snprintf(method, sizeof(method), "%d_workers", w);

                        double total_ms   = r.total_us / 1000.0;
                        double throughput = tasks * 1e6 / r.total_us;
                        long   total_cost = r.mem_run_kb - r.mem_base_kb;

                        fprintf(fp, "%s,%d,%d,%.2f,%.2f,%.4f,%ld\n",
                                method, tasks, run,
                                r.total_us, throughput, r.total_us / tasks,
                                total_cost);

                        printf("%7d  | %9.2f | %22.2f | %+13ld\n",
                               w, total_ms, throughput, total_cost);
                }
                if (!ok) break;
        }

        BENCH_CSV_CLOSE(fp);
        return 0;
}
