#define _GNU_SOURCE
#include "include/work.h"
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

/* ---------- CPU Bound ---------- */

int fibonaccy(int n)
{
        if(n < 2)
                return n;

        return fibonaccy(n-2) + fibonaccy(n-1);
}



/* ---------- I/O Bound ---------- */

int read_file()
{
        int fd = open("benchmark/in/io_test.dat", O_RDONLY);
        if (fd == -1) {
                perror("open benchmark/in/io_test.dat");
                return 1;
        }

        /* Drop the page-cache entry before reading so every call goes to disk,
         * giving realistic I/O latency without needing O_DIRECT. */
        posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);

        char buf[65536];
        ssize_t rb = pread(fd, buf, sizeof(buf), 0);
        if (rb == -1)
                perror("pread");

        close(fd);
        return rb == -1 ? 1 : 0;
}
