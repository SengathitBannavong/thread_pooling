#include "task.h"
#include "thread_pool.h"
#include "unity.h"
#include "threadpool.h"
#include <unistd.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>

static thread_pool_t *pool;
static atomic_int executed_order[100];
static atomic_int executed_idx = 0;

void setUp(void) {
        /* Use 1 worker to make starvation easier to see */
        pool = thread_pool_init(1);
        TEST_ASSERT_NOT_NULL(pool);
        TEST_ASSERT_TRUE(thread_pool_enable_aging(pool, 200L, 400L));
        atomic_store(&executed_idx, 0);
        for (int i = 0; i < 100; i++) atomic_store(&executed_order[i], -1);
}

void tearDown(void) {
        if (pool) thread_pool_destroy(&pool);
}

static inline void sleep_ms(int ms)
{
        struct timespec ts;
        ts.tv_sec = ms / 1000;
        ts.tv_nsec = (long)(ms % 1000) * 1000000L;
        nanosleep(&ts, NULL);
}

void task_sleep(void *arg) {
        int ms = *(int *)arg;
        sleep_ms(ms);
}

void task_log(void *arg) {
        int prio = (int)(intptr_t)arg;
        int idx = atomic_fetch_add(&executed_idx, 1);
        if (idx < 100) {
                atomic_store(&executed_order[idx], prio);
        }
}

void anti_starvation_using_aging(void) {
        int delay = 1000;

        thread_pool_submit(pool, task_sleep, &delay, PRIORITY_HIGH);

        thread_pool_submit(pool, task_log, (void *)(intptr_t)PRIORITY_MEDIUM, PRIORITY_MEDIUM);

        // Step 3: Flood with HIGH priority tasks
        for (int i = 0; i < 10; i++) {
                thread_pool_submit(pool, task_log, (void *)(intptr_t)PRIORITY_HIGH, PRIORITY_HIGH);
        }

        thread_pool_wait(pool);

        int count = atomic_load(&executed_idx);
        printf("\n[INFO] Total tasks executed (excluding sleeper): %d\n", count);

        for (int i = 0; i < count; i++) {
                int p = atomic_load(&executed_order[i]);
                printf("[INFO] Order %d: Priority %s\n", i, (p == (int)PRIORITY_HIGH ? "HIGH" : (p == (int)PRIORITY_MEDIUM ? "MEDIUM" : "UNKNOWN")));
        }

        TEST_ASSERT_EQUAL_INT(11, count);
        TEST_ASSERT_EQUAL_INT((int)PRIORITY_MEDIUM, atomic_load(&executed_order[0]));
}

int main(void) {
        UNITY_BEGIN();
        RUN_TEST(anti_starvation_using_aging);
        return UNITY_END();
}
