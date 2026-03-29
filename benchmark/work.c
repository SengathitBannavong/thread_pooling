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
        int fd = open("benchmark/in/io_test.dat", O_RDONLY | O_DIRECT);
        if (fd == -1) {
                perror("Error opening file with O_DIRECT (check if filesystem supports it)");
                return 1;
        }
        
        void *buf;
        size_t size = 65536;
        if (posix_memalign(&buf, 4096, size) != 0) {
                perror("posix_memalign");
                close(fd);
                return 1;
        }

        // With O_DIRECT, offset and size must be aligned to block size (usually 512 or 4096)
        ssize_t rb = pread(fd, buf, size, 0);
        if (rb == -1) {
                perror("pread (O_DIRECT)");
        }

        free(buf);
        close(fd);
        return rb == -1 ? 1 : 0;
}
