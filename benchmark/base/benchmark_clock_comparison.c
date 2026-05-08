/*
 * benchmark_clock_comparison.c — quantify clock_gettime() cost in the queue
 * push critical section.
 *
 * Hypothesis: calling clock_gettime() inside the queue mutex (as the main
 * thread pool does for submit_time tracking) causes ~2x throughput drop at
 * high worker counts vs the plain baseline pool.
 *
 * Method: compare two pools —
 *   baseline    : q_push holds mutex for pointer updates only
 *   with_clock  : q_push additionally calls clock_gettime() under the lock
 *
 * N=1,000,000 noop tasks, NUM_PRODUCERS concurrent producer threads,
 * 5 trials per (pool, worker_count) pair.  Each trial runs in a forked
 * child for a clean heap.  Results written as CSV + summary table.
 *
 * Output: benchmark/res/result_clock_comparison.txt
 *   pool,workers,trial,ops_per_sec
 */

#define _POSIX_C_SOURCE 199309L
#include "include/bench.h"
#include "baseline/baseline.h"
#include "baseline/baseline_with_clock.h"
#include "cpu_core.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

#define TOTAL_OPS     1000000
#define NUM_PRODUCERS 4
#define NUM_TRIALS    5

static int worker_counts[] = {1, 2, 4, 8, 16};
#define NUM_CONFIGS ((int)(sizeof(worker_counts) / sizeof(worker_counts[0])))

static void noop_task(void *arg) { (void)arg; }

/* ── result sent through pipe ── */
typedef struct { double ops_per_sec; } cmp_result_t;

/* ── baseline producer thread ── */
typedef struct { baseline_pool_t *pool; int n_submit; } prod_base_arg_t;

static void *producer_baseline(void *arg)
{
        prod_base_arg_t *a = arg;
        for (int i = 0; i < a->n_submit; i++)
                baseline_pool_submit(a->pool, noop_task, NULL);
        return NULL;
}

/* ── with_clock producer thread ── */
typedef struct { baseline_wc_pool_t *pool; int n_submit; } prod_wc_arg_t;

static void *producer_wc(void *arg)
{
        prod_wc_arg_t *a = arg;
        for (int i = 0; i < a->n_submit; i++)
                baseline_wc_pool_submit(a->pool, noop_task, NULL);
        return NULL;
}

/* ── child: run baseline pool, send ops/s through pipe ── */
static void child_baseline(int n_workers, int pipefd)
{
        baseline_pool_t *pool = baseline_pool_init(n_workers);
        if (!pool) _exit(1);

        pthread_t       prods[NUM_PRODUCERS];
        prod_base_arg_t pargs[NUM_PRODUCERS];
        int per = TOTAL_OPS / NUM_PRODUCERS;

        double t0 = NOW_US();
        for (int i = 0; i < NUM_PRODUCERS; i++) {
                pargs[i] = (prod_base_arg_t){ pool, per };
                pthread_create(&prods[i], NULL, producer_baseline, &pargs[i]);
        }
        for (int i = 0; i < NUM_PRODUCERS; i++)
                pthread_join(prods[i], NULL);
        baseline_pool_wait(pool);
        double t1 = NOW_US();

        cmp_result_t r = { .ops_per_sec = TOTAL_OPS / ((t1 - t0) / 1e6) };
        (void)write(pipefd, &r, sizeof(r));
        close(pipefd);
        baseline_pool_shutdown(&pool);
        _exit(0);
}

/* ── child: run with_clock pool, send ops/s through pipe ── */
static void child_with_clock(int n_workers, int pipefd)
{
        baseline_wc_pool_t *pool = baseline_wc_pool_init(n_workers);
        if (!pool) _exit(1);

        pthread_t     prods[NUM_PRODUCERS];
        prod_wc_arg_t pargs[NUM_PRODUCERS];
        int per = TOTAL_OPS / NUM_PRODUCERS;

        double t0 = NOW_US();
        for (int i = 0; i < NUM_PRODUCERS; i++) {
                pargs[i] = (prod_wc_arg_t){ pool, per };
                pthread_create(&prods[i], NULL, producer_wc, &pargs[i]);
        }
        for (int i = 0; i < NUM_PRODUCERS; i++)
                pthread_join(prods[i], NULL);
        baseline_wc_pool_wait(pool);
        double t1 = NOW_US();

        cmp_result_t r = { .ops_per_sec = TOTAL_OPS / ((t1 - t0) / 1e6) };
        (void)write(pipefd, &r, sizeof(r));
        close(pipefd);
        baseline_wc_pool_shutdown(&pool);
        _exit(0);
}

/* Fork a child, run the selected pool, return ops/s (or -1 on error). */
static double fork_run(int use_clock, int n_workers)
{
        int fd[2];
        if (pipe(fd) < 0) return -1.0;

        pid_t pid = fork();
        if (pid < 0) { close(fd[0]); close(fd[1]); return -1.0; }
        if (pid == 0) {
                close(fd[0]);
                if (use_clock) child_with_clock(n_workers, fd[1]);
                else           child_baseline(n_workers, fd[1]);
                _exit(0);
        }

        close(fd[1]);
        cmp_result_t r = {0};
        ssize_t got = read(fd[0], &r, sizeof(r));
        close(fd[0]);
        int st; waitpid(pid, &st, 0);
        if (got != (ssize_t)sizeof(r)) return -1.0;
        return r.ops_per_sec;
}

int main(void)
{
        system("mkdir -p benchmark/res");
        FILE *fp = fopen("benchmark/res/result_clock_comparison.txt", "w");
        if (!fp) { perror("fopen"); return 1; }

        fprintf(fp, "pool,workers,trial,ops_per_sec\n");

        double b_mean[NUM_CONFIGS];
        double wc_mean[NUM_CONFIGS];

        printf("=== clock_gettime() Critical-Section Cost Benchmark ===\n");
        printf("N=%d ops | %d producers | %d trials | %d logical cores\n\n",
               TOTAL_OPS, NUM_PRODUCERS, NUM_TRIALS, get_num_core());

        for (int wi = 0; wi < NUM_CONFIGS; wi++) {
                int    w     = worker_counts[wi];
                double bsum  = 0.0;
                double wcsum = 0.0;

                printf("Workers = %d\n", w);
                for (int t = 1; t <= NUM_TRIALS; t++) {
                        double b  = fork_run(0, w);
                        double wc = fork_run(1, w);

                        fprintf(fp, "baseline,%d,%d,%.0f\n",   w, t, b);
                        fprintf(fp, "with_clock,%d,%d,%.0f\n", w, t, wc);

                        printf("  trial %d  baseline=%10.0f ops/s  "
                               "with_clock=%10.0f ops/s  ratio=%.2fx\n",
                               t, b, wc, (b > 0.0 ? wc / b : 0.0));

                        bsum  += b;
                        wcsum += wc;
                }

                b_mean[wi]  = bsum  / NUM_TRIALS;
                wc_mean[wi] = wcsum / NUM_TRIALS;
                printf("\n");
        }

        fclose(fp);

        /* Summary table */
        printf("%-10s %-22s %-22s %-8s\n",
               "Workers", "baseline (ops/s)", "with_clock (ops/s)", "Drop %");
        printf("%-10s %-22s %-22s %-8s\n",
               "-------", "----------------", "------------------", "------");
        for (int wi = 0; wi < NUM_CONFIGS; wi++) {
                double drop = (b_mean[wi] > 0.0)
                        ? (1.0 - wc_mean[wi] / b_mean[wi]) * 100.0 : 0.0;
                printf("%-10d %-22.0f %-22.0f %-8.1f\n",
                       worker_counts[wi], b_mean[wi], wc_mean[wi], drop);
        }

        double drop16 = (b_mean[NUM_CONFIGS - 1] > 0.0)
                ? (1.0 - wc_mean[NUM_CONFIGS - 1] / b_mean[NUM_CONFIGS - 1]) * 100.0
                : 0.0;

        printf("\nAt 16 workers: clock_gettime() in critical section causes %.1f%% "
               "throughput drop.\n", drop16);
        if (drop16 >= 40.0)
                printf("HYPOTHESIS CONFIRMED: clock overhead explains a significant "
                       "portion of the observed 2x gap.\n");
        else
                printf("HYPOTHESIS NOT CONFIRMED: drop is small; investigate "
                       "false sharing or struct-size effects.\n");

        printf("\nResults saved to benchmark/res/result_clock_comparison.txt\n");
        return 0;
}
