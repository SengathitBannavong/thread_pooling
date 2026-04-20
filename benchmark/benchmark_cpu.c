#define _POSIX_C_SOURCE 199309L
#include "include/bench.h"
#include "include/work.h"
#include "../include/threadpool.h"
#include "../include/cpu_core.h"
#include <pthread.h>
#include <stdlib.h>

FILE *fp;

/* ── shared work ── */
static void work_cpu(void *arg) { (void)arg; fibonaccy(30); }
static void *work_pthread(void *arg) { work_cpu(arg); return NULL; }

/* ── argument passed to each forked bench function ── */
typedef struct { int n; int n_cores; } cpu_arg_t;

/* ════════════════════════════════════════════════════════════
 * Each function below runs inside a FORKED child.
 * The child has a fresh copy-on-write heap — no glibc freelist
 * pollution from previous benchmark runs.
 * ════════════════════════════════════════════════════════════ */

static void run_thread_pool(void *_arg, bench_result_t *r)
{
        cpu_arg_t *a = _arg;

        BENCH_MEM_BASELINE();
        thread_pool_t *pool = thread_pool_init(a->n_cores);
        if (!pool) return;
        BENCH_MEM_AFTER_INIT();

        double t0 = NOW_US();
        for (int i = 0; i < a->n; i++)
                thread_pool_submit(pool, work_cpu, NULL, PRIORITY_HIGH);
        thread_pool_wait(pool);
        BENCH_MEM_AFTER_RUN();
        thread_pool_destroy(&pool);
        double t1 = NOW_US();
        BENCH_MEM_AFTER_DESTROY();

        BENCH_RESULT_FILL(r, t0, t1);
}

static void run_on_demand(void *_arg, bench_result_t *r)
{
        cpu_arg_t *a = _arg;
        pthread_t *t = malloc(sizeof(pthread_t) * a->n);
        if (!t) return;

        BENCH_MEM_BASELINE();
        BENCH_MEM_AFTER_INIT();   /* no pool — same as baseline */

        double t0 = NOW_US();
        for (int i = 0; i < a->n; i++)
                pthread_create(&t[i], NULL, work_pthread, NULL);
        for (int i = 0; i < a->n; i++)
                pthread_join(t[i], NULL);
        double t1 = NOW_US();

        BENCH_MEM_AFTER_RUN();
        BENCH_MEM_AFTER_DESTROY();   /* no pool to destroy */

        BENCH_RESULT_FILL(r, t0, t1);
        free(t);
}

static void run_sequential(void *_arg, bench_result_t *r)
{
        cpu_arg_t *a = _arg;

        BENCH_MEM_BASELINE();
        BENCH_MEM_AFTER_INIT();   /* no pool */

        double t0 = NOW_US();
        for (int i = 0; i < a->n; i++)
                work_cpu(NULL);
        double t1 = NOW_US();

        BENCH_MEM_AFTER_RUN();
        BENCH_MEM_AFTER_DESTROY();   /* no pool */

        BENCH_RESULT_FILL(r, t0, t1);
}

/* ════════════════════════════════════════════════════════════
 * Bench wrappers — fork a fresh child, collect result, write CSV
 * ════════════════════════════════════════════════════════════ */

static int thread_pool_bench(int n, int run)
{
        printf("[INFO] start thread_pool\n");
        cpu_arg_t arg = { .n = n, .n_cores = get_num_core() };
        bench_result_t r;
        if (bench_fork(run_thread_pool, &arg, &r) < 0) return -1;
        BENCH_WRITE_RESULT(fp, "thread_pool", n, run, r);
        return 0;
}

static int on_demand_bench(int n, int run)
{
        printf("[INFO] start on_demand\n");
        cpu_arg_t arg = { .n = n };
        bench_result_t r;
        if (bench_fork(run_on_demand, &arg, &r) < 0) return -1;
        BENCH_WRITE_RESULT(fp, "on_demand", n, run, r);
        return 0;
}

static int sequential_bench(int n, int run, int last_run)
{
        printf("[INFO] start sequential\n");
        cpu_arg_t arg = { .n = n };
        bench_result_t r;
        if (bench_fork(run_sequential, &arg, &r) < 0) return -1;
        BENCH_WRITE_RESULT(fp, "sequential", n, run, r);
        BENCH_WRITE_RESULT(fp, "sequential", n, last_run, r);
        return 0;
}

int main(int argc, char *argv[])
{
        if (argc < 3) {
                printf("Usage: %s <n_tasks> <n_runs>\n", argv[0]);
                return -1;
        }

        int n    = atoi(argv[1]);
        int spin = atoi(argv[2]);

        char buffer[256];
        snprintf(buffer, sizeof(buffer),
                 "benchmark/res/result_cpu_bound_n_%d.txt", n);
        system("mkdir -p benchmark/res");

        BENCH_CSV_INIT_MEM(fp, buffer);
        printf("[INFO] device has %d CPU cores\n", get_num_core());

        for (int i = 1; i <= spin; i++) {
                printf("[INFO] spin #%d\n", i);
                if (i == 1 && sequential_bench(n, i, spin)) break;
                if (on_demand_bench(n, i))    break;
                if (thread_pool_bench(n, i))  break;
        }

        BENCH_CSV_CLOSE(fp);
        return 0;
}
