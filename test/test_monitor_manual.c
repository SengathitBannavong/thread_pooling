#define _DEFAULT_SOURCE
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <time.h>
#include "threadpool.h"
#include "monitor.h"
#include "cpu_core.h"

void setUp(void)
{
}

void tearDown(void)
{
}

/**
 * dummy_task - Simulates work by sleeping for 50ms.
 */
static void dummy_task(void *arg) {
    (void)arg;
    struct timespec ts = {0, 50 * 1000000L}; /* 50ms */
    nanosleep(&ts, NULL);
}

int main(void) {
    srand((unsigned)time(NULL));

    int cores = get_num_core();
    if (cores <= 0) cores = 4;

    thread_pool_t *pool = thread_pool_init(cores);
    if (!pool) {
        fprintf(stderr, "Failed to initialize thread pool\n");
        return 1;
    }

    printf("Starting thread pool with %d workers.\n", cores);
    printf("Starting monitor... \n");
    printf("  [h] or [q]  Detach (stops thread, restores terminal)\n");
    printf("  [m]         Reattach monitor (new thread)\n");
    monitor_start(pool);

    while (true) {
        /* Wait until the monitor thread has fully exited and restored the terminal */
        while (thread_pool_monitor_attached(pool))
            sched_yield();

        printf("\n--- Monitor Manual Test Menu ---\n");
        printf(" Status: DETACHED\n");
        printf(" [a] Submit 1000 tasks\n");
        printf(" [m] Reattach monitor\n");
        printf(" [x] Exit test\n");
        printf("Selection: ");
        fflush(stdout);
        char cmd;
        if (scanf(" %c", &cmd) != 1) continue;

        if (cmd == 'a' || cmd == 'A') {
            printf("Submitting 1000 tasks to the pool...\n");
            for (int i = 0; i < 1000; i++) {
                thread_pool_submit(pool, dummy_task, NULL, PRIORITY_MEDIUM);
            }
            printf("Done.\n");
        }
        else if (cmd == 'm' || cmd == 'M') {
            printf("Reattaching monitor...\n");
            if (thread_pool_monitor_reattach(pool) != 0) {
                fprintf(stderr, "Failed to reattach monitor!\n");
            }
        }
        else if (cmd == 'x' || cmd == 'X') {
            printf("Exiting...\n");
            break;
        }
        else {
            printf("Unknown command: %c\n", cmd);
        }
    }

    printf("Cleaning up...\n");
    monitor_stop(pool);
    thread_pool_destroy(&pool);

    return 0;
}
