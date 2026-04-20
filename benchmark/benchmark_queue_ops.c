/*
 * benchmark_queue_ops.c — pure push→pop throughput
 *
 * Each (worker_count × run) is measured in a FORKED child so the heap
 * is always fresh.  Memory numbers are therefore comparable across worker
 * counts without any cross-contamination from previous runs.
 *
 * The child runs both CONCURRENT and ISOLATED modes then reports:
 *
 *   Timing:
 *     conc_push_us / conc_total_us  — submit time / submit+drain time
 *     iso_push_us  / iso_pop_us     — paused-submit time / drain-from-full-queue
 *
 *   Memory (5 points):
 *     mem_base_kb    before pool_init
 *     mem_init_kb    after  pool_init (worker stacks + struct)
 *     mem_peak_kb    after  all N tasks queued, BEFORE workers run (isolated only)
 *                    → mem_peak - mem_init  =  N × sizeof(task_t) in queue
 *     mem_run_kb     after  all tasks executed (queue drained)
 *     mem_destroy_kb after  pool_destroy
 */

#define _POSIX_C_SOURCE 199309L
#include "include/bench.h"
#include "../include/threadpool.h"
#include "../include/cpu_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

static void noop_task(void *arg) { (void)arg; }

/* ── extended result that goes through the pipe ── */
typedef struct {
        double conc_push_us, conc_total_us;
        double iso_push_us,  iso_pop_us;
        long   mem_base_kb, mem_init_kb, mem_peak_kb,
               mem_run_kb,  mem_destroy_kb;
} queue_result_t;

static void bench_concurrent(int n, thread_pool_t *pool,
                              double *push_us, double *total_us)
{
        double t0 = NOW_US();
        for (int i = 0; i < n; i++)
                thread_pool_submit(pool, noop_task, NULL, PRIORITY_MEDIUM);
        double t1 = NOW_US();
        thread_pool_wait(pool);
        double t2 = NOW_US();
        *push_us  = t1 - t0;
        *total_us = t2 - t0;
}

static void bench_isolated(int n, thread_pool_t *pool,
                            double *push_us, double *pop_us, long *peak_kb)
{
        thread_pool_pause(pool);
        double t0 = NOW_US();
        for (int i = 0; i < n; i++)
                thread_pool_submit(pool, noop_task, NULL, PRIORITY_MEDIUM);
        double t1 = NOW_US();
        *peak_kb = _bench_get_rss_kb();   /* all N tasks sitting in queue */
        thread_pool_resume(pool);
        thread_pool_wait(pool);
        double t2 = NOW_US();
        *push_us = t1 - t0;
        *pop_us  = t2 - t1;
}

/* This runs inside a forked child */
static void run_queue_ops(void *_arg, bench_result_t *_unused)
{
        (void)_unused;  /* we use a wider struct sent separately */

        /* arg is actually a queue_ops_child_arg_t* — cast inside */
        typedef struct { int n_tasks; int n_workers; int pipefd; } child_arg_t;
        child_arg_t *a = _arg;

        queue_result_t qr = {0};
        qr.mem_base_kb = _bench_get_rss_kb();

        thread_pool_t *pool = thread_pool_init(a->n_workers);
        if (!pool) { _exit(1); }
        qr.mem_init_kb = _bench_get_rss_kb();

        bench_concurrent(a->n_tasks, pool,
                         &qr.conc_push_us, &qr.conc_total_us);

        bench_isolated(a->n_tasks, pool,
                       &qr.iso_push_us, &qr.iso_pop_us, &qr.mem_peak_kb);

        qr.mem_run_kb = _bench_get_rss_kb();
        thread_pool_destroy(&pool);
        qr.mem_destroy_kb = _bench_get_rss_kb();

        (void)write(a->pipefd, &qr, sizeof(qr));
        close(a->pipefd);
        _exit(0);
}

static int queue_ops_bench(FILE *fp, int n_tasks, int n_workers, int run)
{
        int fd[2];
        if (pipe(fd) < 0) return -1;

        typedef struct { int n_tasks; int n_workers; int pipefd; } child_arg_t;
        child_arg_t arg = { n_tasks, n_workers, fd[1] };

        pid_t pid = fork();
        if (pid < 0) { close(fd[0]); close(fd[1]); return -1; }
        if (pid == 0) {
                close(fd[0]);
                bench_result_t dummy = {0};
                run_queue_ops(&arg, &dummy);
                _exit(0);
        }

        close(fd[1]);
        queue_result_t qr;
        ssize_t got = read(fd[0], &qr, sizeof(qr));
        close(fd[0]);
        int st; waitpid(pid, &st, 0);
        if (got != (ssize_t)sizeof(qr)) return -1;

        /* ── derived stats ── */
        double cp_ops = qr.conc_push_us  > 0 ? n_tasks*1e6/qr.conc_push_us  : 0;
        double cq_ops = qr.conc_total_us > 0 ? n_tasks*1e6/qr.conc_total_us : 0;
        double ip_ops = qr.iso_push_us   > 0 ? n_tasks*1e6/qr.iso_push_us   : 0;
        double io_ops = qr.iso_pop_us    > 0 ? n_tasks*1e6/qr.iso_pop_us    : 0;

        long peak_task_kb  = qr.mem_peak_kb - qr.mem_init_kb;

        char method[32];
        snprintf(method, sizeof(method), "%d_workers", n_workers);

        /* mem_run_cost_kb = peak_task_kb: N tasks sitting in queue (isolated mode).
         * This is the clearest single memory number for queue ops. */
        fprintf(fp, "%s,%d,%d,"
                "%.2f,%.2f,%.2f,%.2f,"
                "%.2f,%.2f,%.2f,%.2f,"
                "%ld\n",
                method, n_tasks, run,
                qr.conc_push_us, qr.conc_total_us, cp_ops, cq_ops,
                qr.iso_push_us,  qr.iso_pop_us,    ip_ops, io_ops,
                peak_task_kb);

        printf("  [%2d workers | run %2d]\n", n_workers, run);
        printf("    CONCURRENT  push=%7.1f us (%8.0f ops/s)  "
               "push+pop=%7.1f us (%8.0f ops/s)\n",
               qr.conc_push_us, cp_ops, qr.conc_total_us, cq_ops);
        printf("    ISOLATED    push=%7.1f us (%8.0f ops/s)  "
               "pop=%7.1f us (%8.0f ops/s)\n",
               qr.iso_push_us, ip_ops, qr.iso_pop_us, io_ops);
        printf("    MEM  peak_task=%+ld KB  (N×task_t in queue)\n\n",
               peak_task_kb);

        return 0;
}

int main(int argc, char *argv[])
{
        if (argc < 3) {
                printf("Usage: %s <n_tasks> <n_runs>\n", argv[0]);
                return 1;
        }

        int n_tasks = atoi(argv[1]);
        int n_runs  = atoi(argv[2]);

        system("mkdir -p benchmark/res");
        FILE *fp = fopen("benchmark/res/result_queue_ops.txt", "w");
        if (!fp) { perror("fopen"); return 1; }

        fprintf(fp, "method,n_tasks,run,"
                "conc_push_us,conc_total_us,"
                "conc_push_ops_per_sec,conc_queue_ops_per_sec,"
                "iso_push_us,iso_pop_us,"
                "iso_push_ops_per_sec,iso_pop_ops_per_sec,"
                "mem_run_cost_kb\n");

        int worker_counts[] = {1, 2, 4, 8, 16};
        int n_configs = (int)(sizeof(worker_counts) / sizeof(worker_counts[0]));

        printf("=== Queue Ops/sec Benchmark (noop tasks, n=%d) ===\n", n_tasks);
        printf("Device: %d logical CPU cores\n\n", get_num_core());

        for (int run = 1; run <= n_runs; run++) {
                printf("══ Run #%d ══════════════════════════════════════════════════\n", run);
                for (int wi = 0; wi < n_configs; wi++) {
                        if (queue_ops_bench(fp, n_tasks, worker_counts[wi], run) < 0) {
                                fprintf(stderr, "[ERROR] bench failed\n");
                                fclose(fp);
                                return 1;
                        }
                }
        }

        fclose(fp);
        printf("Results saved to benchmark/res/result_queue_ops.txt\n");
        return 0;
}
