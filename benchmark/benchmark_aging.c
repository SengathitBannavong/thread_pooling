#define _POSIX_C_SOURCE 199309L
#include "include/bench.h"
#include "include/work.h"
#include "../include/threadpool.h"
#include "../include/cpu_core.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#define N_LOW  5000
#define N_HIGH 10000

static atomic_int g_low_done;
static double     g_low_last_us;
static double     g_start_us;

static void low_work(void *arg)
{
        (void)arg;
        fibonaccy(30);   /* ~1 ms each at -O3 */
        double t = NOW_US();

        int done = atomic_fetch_add_explicit(&g_low_done, 1, memory_order_relaxed) + 1;
        if (done == N_LOW)
                g_low_last_us = t;
}

static void high_work(void *arg)
{
        (void)arg;
        fibonaccy(30);
}

/* ── result sent child → parent through pipe ── */
typedef struct {
        double total_us;
        double low_last_us;  /* relative to g_start_us */
        long   mem_init_kb;
        long   mem_run_kb;
} aging_result_t;

typedef struct {
        int  n_workers;
        bool enable_aging;
        long interval_ms;
        long promote_ms;
        int  pipefd;
} child_arg_t;

static void run_aging_bench(child_arg_t *a)
{
        atomic_store_explicit(&g_low_done, 0, memory_order_relaxed);
        g_low_last_us = 0.0;

        long mem_init_kb = _bench_get_rss_kb();

        thread_pool_t *pool = thread_pool_init(a->n_workers);
        if (!pool) _exit(1);

        if (a->enable_aging)
                thread_pool_enable_aging(pool, a->interval_ms, a->promote_ms);

        /* Pause so ALL tasks are queued before any worker runs.
         * This makes the priority-starvation effect deterministic. */
        thread_pool_pause(pool);

        for (int i = 0; i < N_LOW; i++)
                thread_pool_submit(pool, low_work, NULL, PRIORITY_LOW);
        for (int i = 0; i < N_HIGH; i++)
                thread_pool_submit(pool, high_work, NULL, PRIORITY_HIGH);

        g_start_us = NOW_US();
        thread_pool_resume(pool);
        thread_pool_wait(pool);

        double end_us    = NOW_US();
        long   mem_run_kb = _bench_get_rss_kb();

        thread_pool_destroy(&pool);

        aging_result_t r = {
                .total_us    = end_us - g_start_us,
                .low_last_us = g_low_last_us - g_start_us,
                .mem_init_kb = mem_init_kb,
                .mem_run_kb  = mem_run_kb,
        };

        (void)write(a->pipefd, &r, sizeof(r));
        close(a->pipefd);
        _exit(0);
}

static int do_bench(FILE *fp, const char *method, int n_workers,
                    bool enable_aging, long interval_ms, long promote_ms,
                    int run)
{
        int fd[2];
        if (pipe(fd) < 0) return -1;

        child_arg_t arg = {
                .n_workers    = n_workers,
                .enable_aging = enable_aging,
                .interval_ms  = interval_ms,
                .promote_ms   = promote_ms,
                .pipefd       = fd[1],
        };

        pid_t pid = fork();
        if (pid < 0) { close(fd[0]); close(fd[1]); return -1; }
        if (pid == 0) {
                close(fd[0]);
                run_aging_bench(&arg);
                _exit(0);
        }

        close(fd[1]);
        aging_result_t r;
        ssize_t got = read(fd[0], &r, sizeof(r));
        close(fd[0]);
        int st; waitpid(pid, &st, 0);
        if (got != (ssize_t)sizeof(r)) return -1;

        int    total_tasks = N_LOW + N_HIGH;
        double tps         = total_tasks * 1e6 / r.total_us;
        double avg_lat     = r.total_us / total_tasks;
        long   mem_cost    = r.mem_run_kb - r.mem_init_kb;

        fprintf(fp, "%s,%d,%d,%d,%.2f,%.2f,%.2f,%.2f,%ld\n",
                method, N_LOW, N_HIGH, run,
                r.total_us, tps, avg_lat, r.low_last_us, mem_cost);

        printf("  %-20s  run=%-2d  total=%7.1f ms  low_last=%7.1f ms  mem=%+ld KB\n",
               method, run,
               r.total_us   / 1000.0,
               r.low_last_us / 1000.0,
               mem_cost);

        return 0;
}

int main(void)
{
        int  n_runs      = 5;
        int  n_workers   = get_num_core();
        long interval_ms = 20;    /* aging thread wakes every 20 ms    */
        long promote_ms  = 50;    /* promote tasks waiting > 50 ms      */
        /* LOW tasks reach MEDIUM after 50 ms, then HIGH after another 20 ms.
         * HIGH queue drain time ≈ N_HIGH × ~1 ms / workers ≈ 125 ms (16 cores),
         * so starvation window (without aging) >> promote window (with aging). */

        system("mkdir -p benchmark/res");
        FILE *fp = fopen("benchmark/res/result_aging.txt", "w");
        if (!fp) { perror("fopen"); return 1; }

        fprintf(fp, "method,n_low,n_high,run,"
                    "total_time_us,throughput,avg_latency_us,"
                    "low_last_done_us,mem_run_cost_kb\n");

        printf("=== Aging Scheduler Benchmark ===\n");
        printf("Workers: %d  |  LOW tasks: %d  |  HIGH tasks: %d\n",
               n_workers, N_LOW, N_HIGH);
        printf("Aging: interval=%ld ms, promote_after=%ld ms\n",
               interval_ms, promote_ms);
        printf("(LOW→MEDIUM after %ld ms, MEDIUM→HIGH after another ~%ld ms)\n\n",
               promote_ms, interval_ms);

        for (int run = 1; run <= n_runs; run++) {
                printf("── Run #%d ──\n", run);

                if (do_bench(fp, "aging_disabled", n_workers,
                             false, 0, 0, run) < 0) {
                        fprintf(stderr, "[ERROR] bench failed (disabled)\n");
                        fclose(fp); return 1;
                }
                if (do_bench(fp, "aging_enabled", n_workers,
                             true, interval_ms, promote_ms, run) < 0) {
                        fprintf(stderr, "[ERROR] bench failed (enabled)\n");
                        fclose(fp); return 1;
                }
                printf("\n");
        }

        fclose(fp);
        printf("Results saved to benchmark/res/result_aging.txt\n");
        return 0;
}
