#include "baseline/baseline.h"
#include "unity.h"
#include <stdatomic.h>
#include <stdint.h>

static atomic_int g_counter;

void setUp(void)
{
        atomic_store_explicit(&g_counter, 0, memory_order_relaxed);
}

void tearDown(void) {}

static void increment_counter(void *arg)
{
        (void)arg;
        atomic_fetch_add_explicit(&g_counter, 1, memory_order_relaxed);
}

/* 4 workers, 100k tasks — every task must run exactly once */
void test_smoke_100k_tasks(void)
{
        baseline_pool_t *pool = baseline_pool_init(4);
        TEST_ASSERT_NOT_NULL(pool);

        for (int i = 0; i < 100000; i++)
                baseline_pool_submit(pool, increment_counter, NULL);

        baseline_pool_shutdown(&pool);

        TEST_ASSERT_EQUAL_INT(100000,
                atomic_load_explicit(&g_counter, memory_order_acquire));
        TEST_ASSERT_NULL(pool);
}

/* After shutdown pool is NULL; passing NULL to submit must return -1 */
void test_submit_after_shutdown_returns_neg1(void)
{
        baseline_pool_t *pool = baseline_pool_init(2);
        TEST_ASSERT_NOT_NULL(pool);
        baseline_pool_shutdown(&pool);
        TEST_ASSERT_NULL(pool);

        int64_t ret = baseline_pool_submit(pool, increment_counter, NULL);
        TEST_ASSERT_EQUAL_INT64(-1, ret);
}

/* num_workers == 0 must return NULL */
void test_init_zero_workers_returns_null(void)
{
        baseline_pool_t *p = baseline_pool_init(0);
        TEST_ASSERT_NULL(p);
}

/* num_workers == -1 must return NULL (int32_t param, caught by <= 0 guard) */
void test_init_neg1_workers_returns_null(void)
{
        baseline_pool_t *p = baseline_pool_init(-1);
        TEST_ASSERT_NULL(p);
}

int main(void)
{
        UNITY_BEGIN();

        RUN_TEST(test_smoke_100k_tasks);
        RUN_TEST(test_submit_after_shutdown_returns_neg1);
        RUN_TEST(test_init_zero_workers_returns_null);
        RUN_TEST(test_init_neg1_workers_returns_null);  /* expected to hang/fail */

        return UNITY_END();
}
