#ifndef BENCH_H
#define BENCH_H

#define _POSIX_C_SOURCE 199309L
#include <time.h>
#include <stdio.h>
#include <stdlib.h>


#define BENCH_CSV_INIT(fp, path)                                    \
        do {                                                            \
                (fp) = fopen((path), "w");                                  \
                if (!(fp)) {                                                \
                perror("fopen");                                        \
                exit(EXIT_FAILURE);                                     \
                }                                                           \
                fprintf((fp), "method,n_tasks,run,"                        \
                        "total_time_us,throughput,avg_latency_us\n"); \
        } while(0)

#define BENCH_CSV_CLOSE(fp)     fclose((fp))

/* ── timestamp ── */
#define NOW_US() ({                                         \
        struct timespec _ts;                                    \
        clock_gettime(CLOCK_MONOTONIC, &_ts);                   \
        (_ts.tv_sec * 1e6 + _ts.tv_nsec / 1e3);                \
})

/* ── start / stop ── */
#define BENCH_START()   double _t0 = NOW_US()
#define BENCH_STOP()    double _t1 = NOW_US()

/* ── write to CSV ── */
#define BENCH_WRITE(fp, method, n_tasks, run)               \
        do {                                                    \
                double _total = _t1 - _t0;                         \
                double _tps   = (n_tasks) * 1e6 / _total;          \
                double _lat   = _total / (n_tasks);                 \
                fprintf((fp), "%s,%d,%d,%.2f,%.2f,%.4f\n",         \
                        (method), (n_tasks), (run),                 \
                        _total, _tps, _lat);                        \
        } while(0)
#endif