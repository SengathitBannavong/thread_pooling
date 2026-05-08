#define _POSIX_C_SOURCE 199309L
#include "include/bench.h"
#include "include/work.h"
#include "baseline/baseline.h"
#include <pthread.h>
#include <stdlib.h>

FILE *fp;

static void work_io(void *arg) { (void)arg; read_file(); }
static void *work_pthread(void *arg) { work_io(arg); return NULL; }

typedef struct { int n; } io_arg_t;

static void run_baseline(void *_arg, bench_result_t *r)
{
        io_arg_t *a = _arg;

        BENCH_MEM_BASELINE();
        baseline_pool_t *pool = baseline_pool_init(4);
        if (!pool) return;
        BENCH_MEM_AFTER_INIT();

        double t0 = NOW_US();
        for (int i = 0; i < a->n; i++)
                baseline_pool_submit(pool, work_io, NULL);
        baseline_pool_wait(pool);
        BENCH_MEM_AFTER_RUN();
        baseline_pool_shutdown(&pool);
        double t1 = NOW_US();
        BENCH_MEM_AFTER_DESTROY();

        BENCH_RESULT_FILL(r, t0, t1);
}

static void run_on_demand(void *_arg, bench_result_t *r)
{
        io_arg_t *a = _arg;
        pthread_t *t = malloc(sizeof(pthread_t) * a->n);
        if (!t) return;

        BENCH_MEM_BASELINE();
        BENCH_MEM_AFTER_INIT();

        double t0 = NOW_US();
        for (int i = 0; i < a->n; i++)
                pthread_create(&t[i], NULL, work_pthread, NULL);
        for (int i = 0; i < a->n; i++)
                pthread_join(t[i], NULL);
        double t1 = NOW_US();

        BENCH_MEM_AFTER_RUN();
        BENCH_MEM_AFTER_DESTROY();

        BENCH_RESULT_FILL(r, t0, t1);
        free(t);
}

static void run_sequential(void *_arg, bench_result_t *r)
{
        io_arg_t *a = _arg;

        BENCH_MEM_BASELINE();
        BENCH_MEM_AFTER_INIT();

        double t0 = NOW_US();
        for (int i = 0; i < a->n; i++)
                work_io(NULL);
        double t1 = NOW_US();

        BENCH_MEM_AFTER_RUN();
        BENCH_MEM_AFTER_DESTROY();

        BENCH_RESULT_FILL(r, t0, t1);
}

static int baseline_bench(int n, int run)
{
        printf("[INFO] start baseline\n");
        io_arg_t arg = { .n = n };
        bench_result_t r;
        if (bench_fork(run_baseline, &arg, &r) < 0) return -1;
        BENCH_WRITE_RESULT(fp, "baseline", n, run, r);
        return 0;
}

static int on_demand_bench(int n, int run)
{
        printf("[INFO] start on_demand\n");
        io_arg_t arg = { .n = n };
        bench_result_t r;
        if (bench_fork(run_on_demand, &arg, &r) < 0) return -1;
        BENCH_WRITE_RESULT(fp, "on_demand", n, run, r);
        return 0;
}

static int sequential_bench(int n, int run, int last_run)
{
        printf("[INFO] start sequential\n");
        io_arg_t arg = { .n = n };
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
                 "benchmark/res/result_base_io_bound_n_%d.txt", n);
        system("mkdir -p benchmark/res");

        BENCH_CSV_INIT_MEM(fp, buffer);

        for (int i = 1; i <= spin; i++) {
                printf("[INFO] spin #%d\n", i);
                if (i == 1 && sequential_bench(n, i, spin)) break;
                if (on_demand_bench(n, i))  break;
                if (baseline_bench(n, i))   break;
        }

        BENCH_CSV_CLOSE(fp);
        return 0;
}
