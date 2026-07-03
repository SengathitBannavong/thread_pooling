#define _POSIX_C_SOURCE 199309L
#include "include/bench.h"
#include "include/work.h"
#include "baseline/baseline.h"

/* ════════════════════════════════════════════════════════════
 * Heterogeneous (mixed) workload — baseline FIFO pool.
 *
 * Identical task set and ratios as benchmark_heterogeneous.c, but the
 * baseline pool has no priority API, so every task is submitted FIFO.
 * Comparing the two isolates the cost/benefit of the priority-aware
 * scheduler under a realistic mixed load.
 * ════════════════════════════════════════════════════════════ */

#define N_TASKS 10000

FILE *fp;

static void cpu_heavy(void *arg) { (void)arg; fibonaccy(25); }
static void cpu_light(void *arg) { (void)arg; fibonaccy(18); }
static void io_task(void *arg)   { (void)arg; read_file(); }

typedef struct { int n_tasks; int n_workers; } hetero_arg_t;

static void submit_mixed(baseline_pool_t *pool, int n_tasks)
{
        for (int i = 0; i < n_tasks; i++) {
                switch (i & 3) {
                case 0: baseline_pool_submit(pool, cpu_heavy, NULL); break;
                case 1: baseline_pool_submit(pool, cpu_light, NULL); break;
                case 2: baseline_pool_submit(pool, io_task,   NULL); break;
                default: baseline_pool_submit(pool, cpu_light, NULL); break;
                }
        }
}

static void run_hetero(void *_arg, bench_result_t *r)
{
        hetero_arg_t *a = _arg;

        BENCH_MEM_BASELINE();
        baseline_pool_t *pool = baseline_pool_init(a->n_workers);
        if (!pool) return;
        BENCH_MEM_AFTER_INIT();

        double t0 = NOW_US();
        submit_mixed(pool, a->n_tasks);
        baseline_pool_wait(pool);
        BENCH_MEM_AFTER_RUN();
        baseline_pool_shutdown(&pool);
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
        fp = fopen("benchmark/res/result_base_heterogeneous.txt", "w");
        if (!fp) { perror("fopen"); return 1; }

        fprintf(fp, "method,n_tasks,run,"
                    "total_time_us,throughput,avg_latency_us,"
                    "mem_run_cost_kb\n");

        printf("=== Baseline Heterogeneous (mixed, FIFO) Benchmark ===\n");
        printf("Tasks per sweep point: %d\n\n", N_TASKS);

        for (int run = 1; run <= n_runs; run++) {
                printf("--- Baseline mixed-workload sweep run %d/%d ---\n", run, n_runs);

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

                        double throughput = N_TASKS * 1e6 / r.total_us;
                        double avg_lat    = r.total_us / N_TASKS;
                        long   run_cost   = r.mem_run_kb - r.mem_init_kb;

                        fprintf(fp, "%s,%d,%d,%.2f,%.2f,%.4f,%ld\n",
                                method, N_TASKS, run,
                                r.total_us, throughput, avg_lat, run_cost);

                        printf("  %2d workers : %9.2f ms  | %.2f t/s\n",
                               w, r.total_us / 1000.0, throughput);
                }
                if (!ok) break;
                printf("\n");
        }

        BENCH_CSV_CLOSE(fp);
        printf("Results saved to benchmark/res/result_base_heterogeneous.txt\n");
        return 0;
}
