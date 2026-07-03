#define _POSIX_C_SOURCE 199309L
#include "include/bench.h"
#include "include/work.h"
#include "../include/threadpool.h"
#include "../include/cpu_core.h"

/* ════════════════════════════════════════════════════════════
 * Stability / sustained-load benchmark — priority-aware pool.
 *
 * A single long-lived pool processes BATCH_TASKS tasks per iteration for
 * N_ITERS iterations. Each iteration records its own throughput, latency
 * and the process RSS *drift* relative to the first settled iteration.
 *
 * The goal is not peak throughput but PREDICTABILITY OVER TIME:
 *   - flat throughput across iterations  → no scheduler degradation
 *   - low coefficient of variation (CV)  → jitter is bounded
 *   - flat RSS drift                      → no leak / heap creep
 *
 * Run in-process (no fork isolation): drift across iterations within one
 * living process is exactly what we want to observe. The baseline pool is
 * measured the same way in benchmark_base_stability.c for comparison.
 * ════════════════════════════════════════════════════════════ */

#define N_ITERS     50
#define BATCH_TASKS 5000

static void work_core(void *arg) { (void)arg; fibonaccy(25); }

int main(void)
{
        int n_workers = get_num_core();

        system("mkdir -p benchmark/res");
        FILE *fp = fopen("benchmark/res/result_stability.txt", "w");
        if (!fp) { perror("fopen"); return 1; }

        fprintf(fp, "method,n_tasks,run,"
                    "total_time_us,throughput,avg_latency_us,"
                    "mem_run_cost_kb\n");

        printf("=== Stability / Sustained-load Benchmark (thread pool) ===\n");
        printf("Workers: %d  |  iterations: %d  |  tasks/iter: %d\n\n",
               n_workers, N_ITERS, BATCH_TASKS);

        thread_pool_t *pool = thread_pool_init(n_workers);
        if (!pool) { fprintf(stderr, "[ERROR] pool init failed\n"); fclose(fp); return 1; }

        long rss_settle = 0;   /* RSS baseline taken after iteration 1 */

        for (int it = 1; it <= N_ITERS; it++) {
                double t0 = NOW_US();
                for (int j = 0; j < BATCH_TASKS; j++)
                        thread_pool_submit(pool, work_core, NULL, PRIORITY_MEDIUM);
                thread_pool_wait(pool);
                double t1 = NOW_US();

                long rss_now = _bench_get_rss_kb();
                if (it == 1) rss_settle = rss_now;
                long drift = rss_now - rss_settle;

                double total_us   = t1 - t0;
                double throughput = BATCH_TASKS * 1e6 / total_us;
                double avg_lat    = total_us / BATCH_TASKS;

                fprintf(fp, "threadpool,%d,%d,%.2f,%.2f,%.4f,%ld\n",
                        BATCH_TASKS, it, total_us, throughput, avg_lat, drift);

                printf("iter %2d/%d  | %8.2f ms | %10.2f t/s | RSS drift %+ld KB\n",
                       it, N_ITERS, total_us / 1000.0, throughput, drift);
        }

        thread_pool_destroy(&pool);
        fclose(fp);
        printf("\nResults saved to benchmark/res/result_stability.txt\n");
        return 0;
}
