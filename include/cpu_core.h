#ifndef CPU_CORE_H
#define CPU_CORE_H

#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

static inline int get_num_core(void)
{
        int cores = sysconf(_SC_NPROCESSORS_ONLN);

        return cores;
}

static inline int get_num_thread(void)
{
        FILE *f = fopen("/proc/sys/kernel/threads-max", "r");
        int max_threads;

        if (!f) {
                return -1;
        }

        fscanf(f, "%d", &max_threads);
        fclose(f);

        return max_threads;
}

#endif
