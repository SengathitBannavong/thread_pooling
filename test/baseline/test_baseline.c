#include "baseline/baseline.h"
#include "unity.h"
#include "unity_internals.h"
#include <stdatomic.h>
#include <time.h>

static baseline_pool_t *pool;
static atomic_int g_counter;

void setUp(void)
{
        pool = baseline_pool_init(4);
        atomic_store_explicit(&g_counter, 0, memory_order_relaxed);
}

void tearDown(void)
{
        if (pool)
                baseline_pool_shutdown(&pool);
}

static void increment_counter(void *arg)
{
        (void)arg;
        atomic_fetch_add_explicit(&g_counter, 1, memory_order_relaxed);
}

static void add_value(void *arg)
{
        int *p = (int *)arg;
        atomic_fetch_add_explicit(&g_counter, *p, memory_order_relaxed);
}

static void sleep_then_increment(void *arg)
{
        int ms = *(int *)arg;
        struct timespec ts = { .tv_sec = ms / 1000, .tv_nsec = (long)(ms % 1000) * 1000000L };
        nanosleep(&ts, NULL);
        atomic_fetch_add_explicit(&g_counter, 1, memory_order_relaxed);
}

/* init */

void test_init_valid_returns_non_null(void)
{
        baseline_pool_t *p = baseline_pool_init(2);
        TEST_ASSERT_NOT_NULL(p);
        baseline_pool_shutdown(&p);
}

void test_init_zero_workers_returns_null(void)
{
        baseline_pool_t *p = baseline_pool_init(0);
        TEST_ASSERT_NULL(p);
}

/* submit */

void test_submit_returns_zero(void)
{
        int64_t ret = baseline_pool_submit(pool, increment_counter, NULL);
        TEST_ASSERT_EQUAL_INT64(0, ret);
        baseline_pool_wait(pool);
}

void test_submit_null_pool_returns_error(void)
{
        int64_t ret = baseline_pool_submit(NULL, increment_counter, NULL);
        TEST_ASSERT_EQUAL_INT64(-1, ret);
}

/* execution */

void test_tasks_execute(void)
{
        for (int i = 0; i < 100; i++)
                baseline_pool_submit(pool, increment_counter, NULL);
        baseline_pool_wait(pool);
        TEST_ASSERT_EQUAL_INT(100, atomic_load_explicit(&g_counter, memory_order_acquire));
}

void test_arg_is_passed(void)
{
        static int val = 42;
        baseline_pool_submit(pool, add_value, &val);
        baseline_pool_wait(pool);
        TEST_ASSERT_EQUAL_INT(42, atomic_load_explicit(&g_counter, memory_order_acquire));
}

void test_many_tasks(void)
{
        for (int i = 0; i < 10000; i++)
                baseline_pool_submit(pool, increment_counter, NULL);
        baseline_pool_wait(pool);
        TEST_ASSERT_EQUAL_INT(10000, atomic_load_explicit(&g_counter, memory_order_acquire));
}

/* wait */

void test_wait_completes_all_tasks(void)
{
        static int delay_ms = 10;
        int n = 8;
        for (int i = 0; i < n; i++)
                baseline_pool_submit(pool, sleep_then_increment, &delay_ms);
        baseline_pool_wait(pool);
        TEST_ASSERT_EQUAL_INT(n, atomic_load_explicit(&g_counter, memory_order_acquire));
}

/* shutdown */

void test_shutdown_sets_pool_null(void)
{
        baseline_pool_t *p = baseline_pool_init(2);
        TEST_ASSERT_NOT_NULL(p);
        baseline_pool_shutdown(&p);
        TEST_ASSERT_NULL(p);
}

void test_shutdown_null_pool_is_safe(void)
{
        baseline_pool_shutdown(NULL);
        /* reaching here without crash is the pass condition */
}

int main(void)
{
        UNITY_BEGIN();

        /* init */
        RUN_TEST(test_init_valid_returns_non_null);
        RUN_TEST(test_init_zero_workers_returns_null);

        /* submit */
        RUN_TEST(test_submit_returns_zero);
        RUN_TEST(test_submit_null_pool_returns_error);

        /* execution */
        RUN_TEST(test_tasks_execute);
        RUN_TEST(test_arg_is_passed);
        RUN_TEST(test_many_tasks);

        /* wait */
        RUN_TEST(test_wait_completes_all_tasks);

        /* shutdown */
        RUN_TEST(test_shutdown_sets_pool_null);
        RUN_TEST(test_shutdown_null_pool_is_safe);

        return UNITY_END();
}
