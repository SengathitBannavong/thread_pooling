#define _POSIX_C_SOURCE 199309L
#include "include/bench.h"
#include "include/work.h"
#include "../include/threadpool.h"

/* ════════════════════════════════════════════════════════════
 * Heterogeneous (mixed) workload benchmark — priority-aware pool
 *
 * Unlike the homogeneous cpu/io/scaling benchmarks, this submits an
 * INTERLEAVED blend of three task classes with three priority levels,
 * so the pool's scheduler is exercised the way a real server would
 * exercise it (short and long CPU jobs racing against blocking I/O):
 *
 *   i % 4 == 0 : cpu_heavy  (fib25)  @ PRIORITY_HIGH
 *   i % 4 == 1 : cpu_light  (fib18)  @ PRIORITY_LOW
 *   i % 4 == 2 : io_task    (64KB)   @ PRIORITY_MEDIUM
 *   i % 4 == 3 : cpu_light  (fib18)  @ PRIORITY_MEDIUM
 *
 * The same task set is swept across worker counts {1..64}. The baseline
 * pool runs the identical mix (priority argument dropped, FIFO) in
 * benchmark_base_heterogeneous.c so the two are directly comparable.
 * ════════════════════════════════════════════════════════════ */

#define N_TASKS 10000

FILE *fp;

static void cpu_heavy(void *arg) { (void)arg; fibonaccy(25); }
static void cpu_light(void *arg) { (void)arg; fibonaccy(18); }
static void io_task(void *arg)   { (void)arg; read_file(); }

typedef struct { int n_tasks; int n_workers; } hetero_arg_t;

static void submit_mixed(thread_pool_t *pool, int n_tasks)
{
        for (int i = 0; i < n_tasks; i++) {
                switch (i & 3) {
                case 0: thread_pool_submit(pool, cpu_heavy, NULL, PRIORITY_HIGH);   break;
                case 1: thread_pool_submit(pool, cpu_light, NULL, PRIORITY_LOW);    break;
                case 2: thread_pool_submit(pool, io_task,   NULL, PRIORITY_MEDIUM); break;
                default: thread_pool_submit(pool, cpu_light, NULL, PRIORITY_MEDIUM); break;
                }
        }
}

static void run_hetero(void *_arg, bench_result_t *r)
{
        hetero_arg_t *a = _arg;

        BENCH_MEM_BASELINE();
        thread_pool_t *pool = thread_pool_init(a->n_workers);
        if (!pool) return;
        BENCH_MEM_AFTER_INIT();

        double t0 = NOW_US();
        submit_mixed(pool, a->n_tasks);
        thread_pool_wait(pool);
        BENCH_MEM_AFTER_RUN();
        thread_pool_destroy(&pool);
        double t1 = NOW_US();
        BENCH_MEM_AFTER_DESTROY();

        BENCH_RESULT_FILL(r, t0, t1);
}

int main(int argc, char *argv[])
{
        int n_runs = (argc >= 2) ? atoi(argv[1]) : 1;
        int worker_counts[] = {1, 2, 4, 8, 16, 32, 64};
        int n_counts = (int)(sizeof(worker_counts) / sizeof(int));

        system("mkdir -p benchmark/res");
        fp = fopen("benchmark/res/result_heterogeneous.txt", "w");
        if (!fp) { perror("fopen"); return 1; }

        fprintf(fp, "method,n_tasks,run,"
                    "total_time_us,throughput,avg_latency_us,"
                    "mem_run_cost_kb\n");

        printf("=== Heterogeneous (mixed CPU+IO+priority) Benchmark ===\n");
        printf("Tasks per sweep point: %d  |  mix: 25%% HIGH-cpu, 25%% MED-io, "
               "50%% light-cpu\n\n", N_TASKS);

        for (int run = 1; run <= n_runs; run++) {
                printf("--- Mixed-workload sweep run %d/%d ---\n", run, n_runs);
                printf("%-8s | %-10s | %-22s | %-14s\n",
                       "Workers", "Time (ms)", "Throughput (tasks/s)", "run cost KB");
                printf("--------------------------------------------------------------\n");

                int ok = 1;
                for (int i = 0; i < n_counts; i++) {
                        int w = worker_counts[i];
                        hetero_arg_t arg = { .n_tasks = N_TASKS, .n_workers = w };
                        bench_result_t r;

                        if (bench_fork(run_hetero, &arg, &r) < 0) {
                                fprintf(stderr, "[ERROR] fork failed for %d workers\n", w);
                                ok = 0;
                                break;
                        }

                        char method[32];
                        snprintf(method, sizeof(method), "%d_workers", w);

                        double total_ms   = r.total_us / 1000.0;
                        double throughput = N_TASKS * 1e6 / r.total_us;
                        double avg_lat    = r.total_us / N_TASKS;
                        long   run_cost   = r.mem_run_kb - r.mem_init_kb;

                        fprintf(fp, "%s,%d,%d,%.2f,%.2f,%.4f,%ld\n",
                                method, N_TASKS, run,
                                r.total_us, throughput, avg_lat, run_cost);

                        printf("%7d  | %9.2f | %22.2f | %+13ld\n",
                               w, total_ms, throughput, run_cost);
                }
                if (!ok) break;
                printf("\n");
        }

        BENCH_CSV_CLOSE(fp);
        printf("Results saved to benchmark/res/result_heterogeneous.txt\n");
        return 0;
}
