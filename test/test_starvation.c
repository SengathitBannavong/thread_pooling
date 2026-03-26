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

/**
 * TEST: Starvation
 * 
 * 1. Submit a task that sleeps to occupy the single worker.
 * 2. Submit a LOW priority task.
 * 3. Submit many HIGH priority tasks.
 * 4. Wait for completion.
 * 
 * If the system is strictly priority-based, the LOW task will be
 * processed AFTER ALL HIGH tasks, regardless of submission time.
 */
void test_low_priority_starvation(void) {
        int delay = 50; // 50ms
        
        // Step 1: Occupy worker
        thread_pool_submit(pool, task_sleep, &delay, PRIORITY_HIGH);
        
        // Step 2: Submit LOW priority task (should wait)
        thread_pool_submit(pool, task_log, (void *)(intptr_t)PRIORITY_LOW, PRIORITY_LOW);
        
        // Step 3: Flood with HIGH priority tasks
        for (int i = 0; i < 10; i++) {
                thread_pool_submit(pool, task_log, (void *)(intptr_t)PRIORITY_HIGH, PRIORITY_HIGH);
        }
        
        // Destroy will wait for all tasks to finish
        thread_pool_destroy(&pool);
        pool = NULL; // Prevent double destroy in tearDown
        
        int count = atomic_load(&executed_idx);
        printf("\n[INFO] Total tasks executed (excluding sleeper): %d\n", count);
        
        for (int i = 0; i < count; i++) {
                int p = atomic_load(&executed_order[i]);
                printf("[INFO] Order %d: Priority %s\n", i, (p == (int)PRIORITY_HIGH ? "HIGH" : (p == (int)PRIORITY_LOW ? "LOW" : "UNKNOWN")));
        }
        
        // If starvation exists (strict priority), the LOW task will be the LAST one (index 10).
        // We check if it is indeed the last one.
        TEST_ASSERT_EQUAL_INT(11, count); // 1 low + 10 high = 11
        TEST_ASSERT_EQUAL_INT((int)PRIORITY_LOW, atomic_load(&executed_order[10]));
}

int main(void) {
        UNITY_BEGIN();
        RUN_TEST(test_low_priority_starvation);
        return UNITY_END();
}
