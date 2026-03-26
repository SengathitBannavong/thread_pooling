#include "unity.h"
#include "threadpool.h"
#include <unistd.h>
#include <stdatomic.h>
#include <pthread.h>

static atomic_int executed_count = 0;

void setUp(void)    
{
}

void tearDown(void) 
{
}

static inline void sleep_ms(int ms)
{
        struct timespec ts;
        ts.tv_sec = ms / 1000;
        ts.tv_nsec = (long)(ms % 1000) * 1000000L;
        nanosleep(&ts, NULL);
}

void simple_task(void *arg) {
        (void)arg;
        atomic_fetch_add(&executed_count, 1);
        /* Tiny sleep to increase chance of context switching during drain check */
        sleep_ms(1);
}

/**
 * TEST: Premature Drain Race Condition
 * 
 * Issue: The drain logic checks `pq_size == 0 && total_task_in_system == 0`.
 * There is a gap between a task being popped (pq_size--) and the worker 
 * incrementing total_task_in_system.
 */
void test_premature_drain(void) {
        for (int retry = 0; retry < 10; retry++) {
                atomic_store(&executed_count, 0);
                int num_tasks = 500;
                
                /* Small pool to increase contention */
                thread_pool_t *pool = thread_pool_init(2);
                
                for (int i = 0; i < num_tasks; i++) {
                thread_pool_submit(pool, simple_task, NULL, PRIORITY_MEDIUM);
                }
                
                /* Immediately destroy. 
                If the race condition exists, destroy might exit before all tasks finish. */
                thread_pool_destroy(&pool);
                
                int final_count = atomic_load(&executed_count);
                printf("[RETRY %d] Executed: %d / %d\n", retry, final_count, num_tasks);
                
                /* In a correct implementation, final_count MUST be exactly num_tasks */
                TEST_ASSERT_EQUAL_INT(num_tasks, final_count);
        }
}

/**
 * TEST: Submission vs Destruction Race
 * 
 * This test tries to hit the window where submit checks shutdown=false,
 * but destroy starts and frees the queue before the submit actually pushes.
 * This is hard to trigger but multiple threads help.
 */
static void *submitter_thread(void *arg) {
        thread_pool_t *pool = (thread_pool_t *)arg;
        for (int i = 0; i < 1000; i++) {
                thread_pool_submit(pool, simple_task, NULL, PRIORITY_LOW);
        }
        return NULL;
}

void test_submit_vs_destroy_race(void) {
        /* We run this multiple times to try and hit the race */
        for (int i = 0; i < 20; i++) {
                thread_pool_t *pool = thread_pool_init(4);
                pthread_t sub_thread;
                
                pthread_create(&sub_thread, NULL, submitter_thread, pool);
                
                /* Wait a tiny bit then destroy while sub_thread is likely still submitting */
                sleep_ms(500); 
                thread_pool_destroy(&pool);
                
                pthread_join(sub_thread, NULL);
        }
        /* If it didn't crash, we pass this run. 
        Note: Without the fix, this is prone to SEGV or Deadlock. */
}

int main(void) {
        UNITY_BEGIN();
        RUN_TEST(test_premature_drain);
        RUN_TEST(test_submit_vs_destroy_race);
        return UNITY_END();
}
