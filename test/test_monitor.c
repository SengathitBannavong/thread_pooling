/**
 * test_monitor.c - Interactive test for ncurses thread pool monitor
 *
 * Keys while running:
 *   h - Hide monitor (minimal status bar)
 *   m - Show monitor again
 *   p - Pause thread pool
 *   r - Resume thread pool
 *
 * The test runs for ~25 seconds then exits automatically.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <stdatomic.h>

#include "threadpool.h"
#include "task.h"
#include "cpu_core.h"
#include "monitor.h"

static inline void sleep_ms(int ms)
{
        struct timespec ts;
        ts.tv_sec = ms / 1000;
        ts.tv_nsec = (long)(ms % 1000) * 1000000L;
        nanosleep(&ts, NULL);
}

/* Task that simulates work by sleeping */
static void simulate_work(void *arg)
{
        int ms = *(int *)arg;
        struct timespec ts = {
                .tv_sec = ms / 1000,
                .tv_nsec = (long)(ms % 1000) * 1000000L
        };
        nanosleep(&ts, NULL);
        free(arg);
}

/* Submit random tasks to keep workers busy */
static void submit_random_tasks(thread_pool_t *pool, int count)
{
        for (int i = 0; i < count; i++) {
                int *delay = malloc(sizeof(int));
                *delay = 200 + (rand() % 800); /* 200-1000ms */

                enum task_priority prio = rand() % PRIORITY_COUNT;
                thread_pool_submit(pool, simulate_work, delay, prio);
        }
}

int main(void)
{
        srand((unsigned)time(NULL));

        /* Create thread pool */
        int num_workers = get_num_core();
        if (num_workers <= 0 || num_workers > 16)
                num_workers = 4;

        printf("Creating thread pool with %d workers...\n", num_workers);
        printf("Keys: [h]ide [m]show [p]ause [r]esume\n");
        sleep_ms(2000);

        thread_pool_t *pool = thread_pool_init(num_workers);
        if (!pool) {
                fprintf(stderr, "Failed to create thread pool\n");
                return 1;
        }

        thread_pool_pause(pool);

        /* Start monitor (takes over terminal) */
        monitor_start(pool);

        /* Submit tasks periodically while monitor runs */
        for (int round = 0; round < 50; round++) {
                submit_random_tasks(pool, 3 + (rand() % 5));
        }

        /* Try random printf for test monitor */
        for(int i = 0 ; i < 10; i++)
                printf("------------------------------HI------------------------------\n");

        /* Wait for remaining tasks */
        thread_pool_wait(pool);

        /* Stop monitor and cleanup */
        monitor_stop();
        thread_pool_destroy(&pool);

        printf("Monitor test complete.\n");
        return 0;
}
