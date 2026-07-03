#define _POSIX_C_SOURCE 199309L
#include "include/bench.h"
#include "include/work.h"
#include "baseline/baseline.h"
#include "cpu_core.h"

/* ════════════════════════════════════════════════════════════
 * Stability / sustained-load benchmark — baseline FIFO pool.
 *
 * Same protocol as benchmark_stability.c (one long-lived pool,
 * N_ITERS batches of BATCH_TASKS each, per-iteration throughput and
 * RSS drift) so the predictability (CV) and leak behaviour of the two
 * pools can be compared directly.
 * ════════════════════════════════════════════════════════════ */

#define N_ITERS     50
#define BATCH_TASKS 5000

static void work_core(void *arg) { (void)arg; fibonaccy(25); }

int main(void)
{
        int n_workers = get_num_core();

        system("mkdir -p benchmark/res");
        FILE *fp = fopen("benchmark/res/result_base_stability.txt", "w");
        if (!fp) { perror("fopen"); return 1; }

        fprintf(fp, "method,n_tasks,run,"
                    "total_time_us,throughput,avg_latency_us,"
                    "mem_run_cost_kb\n");

        printf("=== Baseline Stability / Sustained-load Benchmark ===\n");
        printf("Workers: %d  |  iterations: %d  |  tasks/iter: %d\n\n",
               n_workers, N_ITERS, BATCH_TASKS);

        baseline_pool_t *pool = baseline_pool_init(n_workers);
        if (!pool) { fprintf(stderr, "[ERROR] pool init failed\n"); fclose(fp); return 1; }

        long rss_settle = 0;

        for (int it = 1; it <= N_ITERS; it++) {
                double t0 = NOW_US();
                for (int j = 0; j < BATCH_TASKS; j++)
                        baseline_pool_submit(pool, work_core, NULL);
                baseline_pool_wait(pool);
                double t1 = NOW_US();

                long rss_now = _bench_get_rss_kb();
                if (it == 1) rss_settle = rss_now;
                long drift = rss_now - rss_settle;

                double total_us   = t1 - t0;
                double throughput = BATCH_TASKS * 1e6 / total_us;
                double avg_lat    = total_us / BATCH_TASKS;

                fprintf(fp, "baseline,%d,%d,%.2f,%.2f,%.4f,%ld\n",
                        BATCH_TASKS, it, total_us, throughput, avg_lat, drift);

                printf("iter %2d/%d  | %8.2f ms | %10.2f t/s | RSS drift %+ld KB\n",
                       it, N_ITERS, total_us / 1000.0, throughput, drift);
        }

        baseline_pool_shutdown(&pool);
        fclose(fp);
        printf("\nResults saved to benchmark/res/result_base_stability.txt\n");
        return 0;
}
