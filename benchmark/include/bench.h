#ifndef BENCH_H
#define BENCH_H

#define _POSIX_C_SOURCE 199309L
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>


/* ════════════════════════════════════════════════════════════
 * Timestamp helpers
 * ════════════════════════════════════════════════════════════ */

#define NOW_US() ({                                          \
        struct timespec _ts;                                 \
        clock_gettime(CLOCK_MONOTONIC, &_ts);                \
        (_ts.tv_sec * 1e6 + _ts.tv_nsec / 1e3);             \
})

#define BENCH_START()   double _t0 = NOW_US()
#define BENCH_STOP()    double _t1 = NOW_US()


/* ════════════════════════════════════════════════════════════
 * RSS sampler  (Linux /proc/self/status → VmRSS)
 * ════════════════════════════════════════════════════════════ */

static inline long _bench_get_rss_kb(void)
{
        FILE *f = fopen("/proc/self/status", "r");
        if (!f) return -1;
        char line[128];
        long kb = -1;
        while (fgets(line, sizeof(line), f)) {
                if (strncmp(line, "VmRSS:", 6) == 0) {
                        sscanf(line + 6, " %ld", &kb);
                        break;
                }
        }
        fclose(f);
        return kb;
}


/* ════════════════════════════════════════════════════════════
 * Result struct — filled by bench function running in the child
 *
 * 4-point memory layout (child fills all four points):
 *
 *   mem_base_kb    ← before pool_init()
 *   mem_init_kb    ← after  pool_init()   (struct + worker stacks)
 *   mem_run_kb     ← after  tasks done    (pool still alive)
 *   mem_destroy_kb ← after  pool_destroy()
 *
 * total_us = wall time of the timed region (submit → tasks done → destroy)
 * ════════════════════════════════════════════════════════════ */

typedef struct {
        double total_us;
        long   mem_base_kb;
        long   mem_init_kb;
        long   mem_run_kb;
        long   mem_destroy_kb;
} bench_result_t;

/* Convenience aliases used inside child bench functions */
#define BENCH_MEM_BASELINE()      long _mem_base_kb    = _bench_get_rss_kb()
#define BENCH_MEM_AFTER_INIT()    long _mem_init_kb    = _bench_get_rss_kb()
#define BENCH_MEM_AFTER_RUN()     long _mem_run_kb     = _bench_get_rss_kb()
#define BENCH_MEM_AFTER_DESTROY() long _mem_destroy_kb = _bench_get_rss_kb()

/* Copy locals into bench_result_t* r (call after all 4 macros + timing) */
#define BENCH_RESULT_FILL(r, t0, t1) \
        do { \
                (r)->total_us      = (t1) - (t0); \
                (r)->mem_base_kb    = _mem_base_kb; \
                (r)->mem_init_kb    = _mem_init_kb; \
                (r)->mem_run_kb     = _mem_run_kb; \
                (r)->mem_destroy_kb = _mem_destroy_kb; \
        } while(0)


/* ════════════════════════════════════════════════════════════
 * Fork runner
 *
 * bench_fork(fn, arg, out):
 *   - forks a child process
 *   - child calls fn(arg, &r), fills bench_result_t r, sends via pipe
 *   - parent waits, reads result into *out
 *   - returns 0 on success, -1 on error
 *
 * Each fork gets a FRESH heap (copy-on-write from parent) so glibc's
 * freelist is NOT polluted by previous benchmark runs.  This ensures
 * that the RSS delta of each method is independent and comparable.
 * ════════════════════════════════════════════════════════════ */

typedef void (*bench_fn_t)(void *arg, bench_result_t *out);

static inline int bench_fork(bench_fn_t fn, void *arg, bench_result_t *out)
{
        int fd[2];
        if (pipe(fd) < 0) return -1;

        pid_t pid = fork();
        if (pid < 0) {
                close(fd[0]); close(fd[1]);
                return -1;
        }

        if (pid == 0) {
                /* ── child ── */
                close(fd[0]);
                bench_result_t r = {0};
                fn(arg, &r);
                (void)write(fd[1], &r, sizeof(r));
                close(fd[1]);
                _exit(0);   /* _exit: skip atexit/stdio flush so parent's fp is safe */
        }

        /* ── parent ── */
        close(fd[1]);
        ssize_t got = read(fd[0], out, sizeof(*out));
        close(fd[0]);
        int st;
        waitpid(pid, &st, 0);
        return (got == (ssize_t)sizeof(*out)) ? 0 : -1;
}


/* ════════════════════════════════════════════════════════════
 * Basic CSV  (timing only)
 *   method,n_tasks,run,total_time_us,throughput,avg_latency_us
 * ════════════════════════════════════════════════════════════ */

#define BENCH_CSV_INIT(fp, path)                                        \
        do {                                                            \
                (fp) = fopen((path), "w");                              \
                if (!(fp)) { perror("fopen"); exit(EXIT_FAILURE); }     \
                fprintf((fp), "method,n_tasks,run,"                    \
                        "total_time_us,throughput,avg_latency_us\n");  \
        } while(0)

#define BENCH_CSV_CLOSE(fp)     fclose((fp))

#define BENCH_WRITE(fp, method, n_tasks, run)                          \
        do {                                                            \
                double _total = _t1 - _t0;                             \
                double _tps   = (n_tasks) * 1e6 / _total;              \
                double _lat   = _total / (n_tasks);                    \
                fprintf((fp), "%s,%d,%d,%.2f,%.2f,%.4f\n",            \
                        (method), (n_tasks), (run),                    \
                        _total, _tps, _lat);                           \
        } while(0)


/* ════════════════════════════════════════════════════════════
 * Extended CSV  (timing + single memory column)
 *
 * Columns:
 *   method,n_tasks,run,total_time_us,throughput,avg_latency_us,mem_run_cost_kb
 *
 * mem_run_cost_kb = mem_run - mem_init
 *   = extra RSS consumed while tasks were executing, measured in an
 *     isolated forked child (clean heap, no cross-benchmark pollution).
 *   = the one number that answers "how much memory does this method use?"
 *
 * All four sample points are still collected internally in bench_result_t
 * so nothing is lost — we just only write the useful one.
 * ════════════════════════════════════════════════════════════ */

#define BENCH_CSV_INIT_MEM(fp, path)                                    \
        do {                                                            \
                (fp) = fopen((path), "w");                              \
                if (!(fp)) { perror("fopen"); exit(EXIT_FAILURE); }     \
                fprintf((fp), "method,n_tasks,run,"                    \
                        "total_time_us,throughput,avg_latency_us,"     \
                        "mem_run_cost_kb\n");                          \
        } while(0)

/* Write one CSV row from a bench_result_t filled by bench_fork() */
#define BENCH_WRITE_RESULT(fp, method, n_tasks, run, r)                \
        do {                                                            \
                double _tps = (n_tasks) * 1e6 / (r).total_us;         \
                double _lat = (r).total_us / (n_tasks);                \
                long   _mem = (r).mem_run_kb - (r).mem_init_kb;        \
                fprintf((fp), "%s,%d,%d,%.2f,%.2f,%.4f,%ld\n",        \
                        (method), (n_tasks), (run),                    \
                        (r).total_us, _tps, _lat, _mem);               \
        } while(0)

#endif
