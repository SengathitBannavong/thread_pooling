#include "unity.h"
#include "threadpool.h"

static struct priority_queue_t pq;

void setUp(void)
{
    pq_init(&pq);
}

void tearDown(void)
{
    /* drain any leftover tasks to avoid leaks */
    struct task_t *t;
    while ((t = pq_pop_nonblock(&pq)) != NULL) {
        task_destroy(t);
    }
    pq_destroy(&pq);
}

static void dummy_func(void *arg) { (void)arg; }

static struct task_t *make_task(enum task_priority p)
{
    return task_create(dummy_func, NULL, p);
}

void test_pq_init_is_empty(void)
{
    TEST_ASSERT_TRUE(pq_is_empty(&pq));
}

void test_pq_init_size_is_zero(void)
{
    TEST_ASSERT_EQUAL_INT(0, pq_size(&pq));
}


void test_pq_push_increases_size(void)
{
    struct task_t *t = make_task(PRIORITY_LOW);
    pq_push(&pq, t);
    TEST_ASSERT_EQUAL_INT(1, pq_size(&pq));
}

void test_pq_push_three_increases_size(void)
{
    pq_push(&pq, make_task(PRIORITY_LOW));
    pq_push(&pq, make_task(PRIORITY_MEDIUM));
    pq_push(&pq, make_task(PRIORITY_HIGH));
    TEST_ASSERT_EQUAL_INT(3, pq_size(&pq));
}

void test_pq_not_empty_after_push(void)
{
    pq_push(&pq, make_task(PRIORITY_LOW));
    TEST_ASSERT_FALSE(pq_is_empty(&pq));
}

void test_pq_high_before_med(void)
{
    pq_push(&pq, make_task(PRIORITY_MEDIUM));
    pq_push(&pq, make_task(PRIORITY_HIGH));

    struct task_t *t = pq_pop_nonblock(&pq);
    TEST_ASSERT_EQUAL_INT(PRIORITY_HIGH, t->priority);
    task_destroy(t);
}

void test_pq_high_before_low(void)
{
    pq_push(&pq, make_task(PRIORITY_LOW));
    pq_push(&pq, make_task(PRIORITY_HIGH));

    struct task_t *t = pq_pop_nonblock(&pq);
    TEST_ASSERT_EQUAL_INT(PRIORITY_HIGH, t->priority);
    task_destroy(t);
}

void test_pq_med_before_low(void)
{
    pq_push(&pq, make_task(PRIORITY_LOW));
    pq_push(&pq, make_task(PRIORITY_MEDIUM));

    struct task_t *t = pq_pop_nonblock(&pq);
    TEST_ASSERT_EQUAL_INT(PRIORITY_MEDIUM, t->priority);
    task_destroy(t);
}

void test_pq_full_ordering(void)
{
    /* push in worst order */
    pq_push(&pq, make_task(PRIORITY_LOW));
    pq_push(&pq, make_task(PRIORITY_HIGH));
    pq_push(&pq, make_task(PRIORITY_MEDIUM));

    struct task_t *t1 = pq_pop_nonblock(&pq);
    struct task_t *t2 = pq_pop_nonblock(&pq);
    struct task_t *t3 = pq_pop_nonblock(&pq);

    TEST_ASSERT_EQUAL_INT(PRIORITY_HIGH, t1->priority);
    TEST_ASSERT_EQUAL_INT(PRIORITY_MEDIUM,  t2->priority);
    TEST_ASSERT_EQUAL_INT(PRIORITY_LOW,  t3->priority);

    task_destroy(t1);
    task_destroy(t2);
    task_destroy(t3);
}


void test_pq_fifo_within_same_priority(void)
{
    struct task_t *a = make_task(PRIORITY_MEDIUM);
    struct task_t *b = make_task(PRIORITY_MEDIUM);
    struct task_t *c = make_task(PRIORITY_MEDIUM);

    pq_push(&pq, a);
    pq_push(&pq, b);
    pq_push(&pq, c);

    /* task_id is monotonically increasing — first pushed = lowest id */
    struct task_t *first  = pq_pop_nonblock(&pq);
    struct task_t *second = pq_pop_nonblock(&pq);
    struct task_t *third  = pq_pop_nonblock(&pq);

    TEST_ASSERT_EQUAL_UINT64(a->task_id, first->task_id);
    TEST_ASSERT_EQUAL_UINT64(b->task_id, second->task_id);
    TEST_ASSERT_EQUAL_UINT64(c->task_id, third->task_id);

    task_destroy(first);
    task_destroy(second);
    task_destroy(third);
}

void test_pq_pop_nonblock_empty_returns_null(void)
{
    struct task_t *t = pq_pop_nonblock(&pq);
    TEST_ASSERT_NULL(t);
}

void test_pq_pop_nonblock_drains_to_empty(void)
{
    pq_push(&pq, make_task(PRIORITY_LOW));
    pq_push(&pq, make_task(PRIORITY_LOW));

    struct task_t *t1 = pq_pop_nonblock(&pq);
    struct task_t *t2 = pq_pop_nonblock(&pq);
    struct task_t *t3 = pq_pop_nonblock(&pq); /* must be NULL */

    TEST_ASSERT_NOT_NULL(t1);
    TEST_ASSERT_NOT_NULL(t2);
    TEST_ASSERT_NULL(t3);

    task_destroy(t1);
    task_destroy(t2);
}

void test_pq_size_decreases_after_pop(void)
{
    pq_push(&pq, make_task(PRIORITY_HIGH));
    pq_push(&pq, make_task(PRIORITY_LOW));
    TEST_ASSERT_EQUAL_INT(2, pq_size(&pq));

    struct task_t *t = pq_pop_nonblock(&pq);
    task_destroy(t);
    TEST_ASSERT_EQUAL_INT(1, pq_size(&pq));
}

void test_pq_empty_after_full_drain(void)
{
    pq_push(&pq, make_task(PRIORITY_MEDIUM));
    struct task_t *t = pq_pop_nonblock(&pq);
    task_destroy(t);
    TEST_ASSERT_TRUE(pq_is_empty(&pq));
    TEST_ASSERT_EQUAL_INT(0, pq_size(&pq));
}


int main(void)
{
    UNITY_BEGIN();

    /* init */
    RUN_TEST(test_pq_init_is_empty);
    RUN_TEST(test_pq_init_size_is_zero);

    /* push / size */
    RUN_TEST(test_pq_push_increases_size);
    RUN_TEST(test_pq_push_three_increases_size);
    RUN_TEST(test_pq_not_empty_after_push);

    /* priority ordering */
    RUN_TEST(test_pq_high_before_med);
    RUN_TEST(test_pq_high_before_low);
    RUN_TEST(test_pq_med_before_low);
    RUN_TEST(test_pq_full_ordering);

    /* FIFO within same priority */
    RUN_TEST(test_pq_fifo_within_same_priority);

    /* pop_nonblock */
    RUN_TEST(test_pq_pop_nonblock_empty_returns_null);
    RUN_TEST(test_pq_pop_nonblock_drains_to_empty);

    /* size after pop */
    RUN_TEST(test_pq_size_decreases_after_pop);
    RUN_TEST(test_pq_empty_after_full_drain);

    return UNITY_END();
}