#define _DEFAULT_SOURCE
#include "unity.h"
#include "threadpool.h"
#include "monitor.h"
#include "cpu_core.h"
#include <unistd.h>
#include <stdlib.h>
#include <time.h>

static thread_pool_t *pool;

void setUp(void)
{
    int num_workers = get_num_core();
    if (num_workers <= 0 || num_workers > 16)
        num_workers = 4;
    pool = thread_pool_init(num_workers);
}

void tearDown(void)
{
    if (thread_pool_monitor_attached(pool)) {
        monitor_stop(pool);
    }
    thread_pool_destroy(&pool);
}

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

static void submit_random_tasks(thread_pool_t *pool, int count)
{
    for (int i = 0; i < count; i++) {
        int *delay = malloc(sizeof(int));
        *delay = 50 + (rand() % 150); /* 50-200ms */

        enum task_priority prio = rand() % PRIORITY_COUNT;
        thread_pool_submit(pool, simulate_work, delay, prio);
    }
}

void test_monitor_start_stop(void)
{
    TEST_ASSERT_FALSE(thread_pool_monitor_attached(pool));

    monitor_start(pool);
    TEST_ASSERT_TRUE(thread_pool_monitor_attached(pool));

    /* Let it run for a short bit */
    usleep(200000);

    monitor_stop(pool);
    TEST_ASSERT_FALSE(thread_pool_monitor_attached(pool));
}

void test_monitor_detach_reattach(void)
{
    monitor_start(pool);
    TEST_ASSERT_TRUE(thread_pool_monitor_attached(pool));

    int res = thread_pool_monitor_detach(pool);
    TEST_ASSERT_EQUAL_INT(0, res);
    TEST_ASSERT_FALSE(thread_pool_monitor_attached(pool));

    res = thread_pool_monitor_reattach(pool);
    TEST_ASSERT_EQUAL_INT(0, res);
    TEST_ASSERT_TRUE(thread_pool_monitor_attached(pool));

    monitor_stop(pool);
    TEST_ASSERT_FALSE(thread_pool_monitor_attached(pool));
}

void test_monitor_double_start_ignored(void)
{
    monitor_start(pool);
    TEST_ASSERT_TRUE(thread_pool_monitor_attached(pool));

    /* Should not crash or create multiple threads */
    monitor_start(pool);
    TEST_ASSERT_TRUE(thread_pool_monitor_attached(pool));

    monitor_stop(pool);
}

void test_monitor_reattach_without_start_fails(void)
{
    int res = thread_pool_monitor_reattach(pool);
    TEST_ASSERT_EQUAL_INT(-1, res);
    TEST_ASSERT_FALSE(thread_pool_monitor_attached(pool));
}

void test_monitor_detach_not_attached_fails(void)
{
    int res = thread_pool_monitor_detach(pool);
    TEST_ASSERT_EQUAL_INT(-1, res);
}

void test_monitor_with_activity(void)
{
    monitor_start(pool);

    /* Submit some tasks to see if monitor survives activity */
    submit_random_tasks(pool, 20);

    /* Let it run and draw a few times */
    usleep(500000);

    thread_pool_wait(pool);

    monitor_stop(pool);
    TEST_ASSERT_FALSE(thread_pool_monitor_attached(pool));
}

int main(void)
{
    srand((unsigned)time(NULL));
    UNITY_BEGIN();

    RUN_TEST(test_monitor_start_stop);
    RUN_TEST(test_monitor_detach_reattach);
    RUN_TEST(test_monitor_double_start_ignored);
    RUN_TEST(test_monitor_reattach_without_start_fails);
    RUN_TEST(test_monitor_detach_not_attached_fails);
    RUN_TEST(test_monitor_with_activity);

    return UNITY_END();
}
