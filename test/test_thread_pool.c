#include "unity.h"
#include "threadpool.h"

static thread_pool_t *pool;

void setUp(void)
{
        int workers = (int)sysconf(_SC_NPROCESSORS_ONLN);
        if (workers <= 0) workers = 2;
        pool = thread_pool_init(workers);
}

void tearDown(void)
{
        thread_pool_destroy(&pool);
}

/* atomic counter — tasks increment this on execute */
static atomic_int g_counter;

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

static void sleep_ms(void *arg)
{
        int ms = *(int *)arg;
        struct timespec ts;
        ts.tv_sec = ms / 1000;
        ts.tv_nsec = (long)(ms % 1000) * 1000000L;
        nanosleep(&ts, NULL);
}

void test_pool_init_returns_valid_pointer(void)
{
        /* pool already inited in setUp — test a second fresh one */
        thread_pool_t *p2 = thread_pool_init(2);
        TEST_ASSERT_NOT_NULL(p2);
        thread_pool_destroy(&p2);
}

void test_pool_init_zero_workers_returns_null(void)
{
        thread_pool_t *p2 = thread_pool_init(0);
        TEST_ASSERT_NULL(p2);
}

void test_submit_returns_valid_id(void)
{
        int64_t id = thread_pool_submit(pool, increment_counter, NULL,
                                        PRIORITY_LOW);
        TEST_ASSERT_GREATER_OR_EQUAL_INT64(0, id);
}

void test_submit_null_pool_returns_error(void)
{
        int64_t id = thread_pool_submit(NULL, increment_counter, NULL,
                                        PRIORITY_LOW);
        TEST_ASSERT_EQUAL_INT64(-1, id);
}

void test_submit_null_func_returns_error(void)
{
        int64_t id = thread_pool_submit(pool, NULL, NULL, PRIORITY_LOW);
        TEST_ASSERT_EQUAL_INT64(-1, id);
}

void test_submit_ids_are_unique(void)
{
        int64_t a = thread_pool_submit(pool, increment_counter, NULL, PRIORITY_LOW);
        int64_t b = thread_pool_submit(pool, increment_counter, NULL, PRIORITY_LOW);
        int64_t c = thread_pool_submit(pool, increment_counter, NULL, PRIORITY_LOW);

        TEST_ASSERT_NOT_EQUAL(a, b);
        TEST_ASSERT_NOT_EQUAL(b, c);
        TEST_ASSERT_NOT_EQUAL(a, c);
}

void test_single_task_executes(void)
{
        atomic_store(&g_counter, 0);
        thread_pool_submit(pool, increment_counter, NULL, PRIORITY_LOW);

        /* destroy waits for all tasks to finish */
        thread_pool_destroy(&pool);
        TEST_ASSERT_EQUAL_INT(1, atomic_load(&g_counter));

        /* re-init for tearDown */
        int workers = (int)sysconf(_SC_NPROCESSORS_ONLN);
        if (workers <= 0) workers = 2;
        pool = thread_pool_init(workers);
}

void test_many_tasks_all_execute(void)
{
        atomic_store(&g_counter, 0);
        const int N = 100;

        for (int i = 0; i < N; i++) {
                thread_pool_submit(pool, increment_counter, NULL, PRIORITY_MEDIUM);
        }

        thread_pool_destroy(&pool);
        TEST_ASSERT_EQUAL_INT(N, atomic_load(&g_counter));

        int workers = (int)sysconf(_SC_NPROCESSORS_ONLN);
        if (workers <= 0) workers = 2;
        pool = thread_pool_init(workers);
}

void test_task_receives_correct_arg(void)
{
        atomic_store(&g_counter, 0);
        int val = 7;
        thread_pool_submit(pool, add_value, &val, PRIORITY_HIGH);

        thread_pool_destroy(&pool);
        TEST_ASSERT_EQUAL_INT(7, atomic_load(&g_counter));

        int workers = (int)sysconf(_SC_NPROCESSORS_ONLN);
        if (workers <= 0) workers = 2;
        pool = thread_pool_init(workers);
}

/*
 * Strategy: fill the queue faster than workers can drain it, then
 * record execution order via a shared log.
 *
 * We use 1 worker to force serial execution so order is deterministic.
 */

#define ORDER_LOG_SIZE 9
static int          g_order_log[ORDER_LOG_SIZE];
static atomic_int   g_order_idx;

static void log_priority(void *arg)
{
        int p = *(int *)arg;
        int idx = atomic_fetch_add_explicit(&g_order_idx, 1,
                                                memory_order_relaxed);
        if (idx < ORDER_LOG_SIZE)
                g_order_log[idx] = p;
}

void test_priority_order_high_before_low(void)
{
        /* single-worker pool for deterministic ordering */
        thread_pool_t *p1 = thread_pool_init(1);

        atomic_store(&g_order_idx, 0);

        /* submit a blocking task first to hold the single worker,
        * giving us time to fill the queue */
        int delay = 50; /* ms */
        thread_pool_submit(p1, sleep_ms, &delay, PRIORITY_LOW);

        /* now flood the queue while worker is sleeping */
        static int lo = PRIORITY_LOW, md = PRIORITY_MEDIUM, hi = PRIORITY_HIGH;
        thread_pool_submit(p1, log_priority, &lo, PRIORITY_LOW);
        thread_pool_submit(p1, log_priority, &lo, PRIORITY_LOW);
        thread_pool_submit(p1, log_priority, &lo, PRIORITY_LOW);
        thread_pool_submit(p1, log_priority, &md, PRIORITY_MEDIUM);
        thread_pool_submit(p1, log_priority, &md, PRIORITY_MEDIUM);
        thread_pool_submit(p1, log_priority, &md, PRIORITY_MEDIUM);
        thread_pool_submit(p1, log_priority, &hi, PRIORITY_HIGH);
        thread_pool_submit(p1, log_priority, &hi, PRIORITY_HIGH);
        thread_pool_submit(p1, log_priority, &hi, PRIORITY_HIGH);

        thread_pool_destroy(&p1);

        /* first 3 should be HIGH, next 3 MED, last 3 LOW */
        for (int i = 0; i < 3; i++)
                TEST_ASSERT_EQUAL_INT(PRIORITY_HIGH, g_order_log[i]);
        for (int i = 3; i < 6; i++)
                TEST_ASSERT_EQUAL_INT(PRIORITY_MEDIUM, g_order_log[i]);
        for (int i = 6; i < 9; i++)
                TEST_ASSERT_EQUAL_INT(PRIORITY_LOW, g_order_log[i]);
}

void test_submit_after_destroy_returns_error(void)
{
        thread_pool_destroy(&pool);
        int64_t id = thread_pool_submit(pool, increment_counter, NULL,
                                        PRIORITY_LOW);
        TEST_ASSERT_EQUAL_INT64(-1, id);

        /* re-init for tearDown */
        int workers = (int)sysconf(_SC_NPROCESSORS_ONLN);
        if (workers <= 0) workers = 2;
        pool = thread_pool_init(workers);
}

void test_destroy_waits_for_running_tasks(void)
{
        atomic_store(&g_counter, 0);
        int delay = 30; /* ms — short but non-zero */

        for (int i = 0; i < 4; i++) {
                thread_pool_submit(pool, sleep_ms, &delay, PRIORITY_LOW);
        }
        /* submit a counter task after the sleepers */
        thread_pool_submit(pool, increment_counter, NULL, PRIORITY_LOW);

        thread_pool_destroy(&pool);

        /* counter must be 1 — destroy must have waited */
        TEST_ASSERT_EQUAL_INT(1, atomic_load(&g_counter));

        int workers = (int)sysconf(_SC_NPROCESSORS_ONLN);
        if (workers <= 0) workers = 2;
        pool = thread_pool_init(workers);
}


int main(void)
{
        UNITY_BEGIN();

        /* init */
        RUN_TEST(test_pool_init_returns_valid_pointer);
        RUN_TEST(test_pool_init_zero_workers_returns_null);

        /* submit */
        RUN_TEST(test_submit_returns_valid_id);
        RUN_TEST(test_submit_null_pool_returns_error);
        RUN_TEST(test_submit_null_func_returns_error);
        RUN_TEST(test_submit_ids_are_unique);

        /* execution */
        RUN_TEST(test_single_task_executes);
        RUN_TEST(test_many_tasks_all_execute);
        RUN_TEST(test_task_receives_correct_arg);

        /* priority ordering */
        RUN_TEST(test_priority_order_high_before_low);

        /* shutdown */
        RUN_TEST(test_submit_after_destroy_returns_error);
        RUN_TEST(test_destroy_waits_for_running_tasks);

        return UNITY_END();
}