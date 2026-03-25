#include "unity.h"
#include "threadpool.h"

void setUp(void)    
{
}

void tearDown(void) 
{
}

static void dummy_func(void *arg)
{
        (void)arg;
}

void test_task_create_returns_non_null(void)
{
        struct task_t *t = task_create(dummy_func, NULL, PRIORITY_LOW);
        TEST_ASSERT_NOT_NULL(t);
        task_destroy(t);
}

void test_task_create_sets_func(void)
{
        struct task_t *t = task_create(dummy_func, NULL, PRIORITY_LOW);
        TEST_ASSERT_EQUAL_PTR(dummy_func, t->func);
        task_destroy(t);
}

void test_task_create_sets_arg(void)
{
        int x = 42;
        struct task_t *t = task_create(dummy_func, &x, PRIORITY_LOW);
        TEST_ASSERT_EQUAL_PTR(&x, t->arg);
        task_destroy(t);
}

void test_task_create_null_arg_allowed(void)
{
        struct task_t *t = task_create(dummy_func, NULL, PRIORITY_MEDIUM);
        TEST_ASSERT_NOT_NULL(t);
        TEST_ASSERT_NULL(t->arg);
        task_destroy(t);
}

void test_task_create_sets_priority_low(void)
{
        struct task_t *t = task_create(dummy_func, NULL, PRIORITY_LOW);
        TEST_ASSERT_EQUAL_INT(PRIORITY_LOW, t->priority);
        task_destroy(t);
}

void test_task_create_sets_priority_med(void)
{
        struct task_t *t = task_create(dummy_func, NULL, PRIORITY_MEDIUM);
        TEST_ASSERT_EQUAL_INT(PRIORITY_MEDIUM, t->priority);
        task_destroy(t);
}

void test_task_create_sets_priority_high(void)
{
        struct task_t *t = task_create(dummy_func, NULL, PRIORITY_HIGH);
        TEST_ASSERT_EQUAL_INT(PRIORITY_HIGH, t->priority);
        task_destroy(t);
}

void test_task_create_next_is_null(void)
{
        struct task_t *t = task_create(dummy_func, NULL, PRIORITY_LOW);
        TEST_ASSERT_NULL(t->next);
        task_destroy(t);
}

void test_task_ids_are_unique(void)
{
        struct task_t *a = task_create(dummy_func, NULL, PRIORITY_LOW);
        struct task_t *b = task_create(dummy_func, NULL, PRIORITY_LOW);
        struct task_t *c = task_create(dummy_func, NULL, PRIORITY_LOW);

        TEST_ASSERT_NOT_EQUAL(a->task_id, b->task_id);
        TEST_ASSERT_NOT_EQUAL(b->task_id, c->task_id);
        TEST_ASSERT_NOT_EQUAL(a->task_id, c->task_id);

        task_destroy(a);
        task_destroy(b);
        task_destroy(c);
}

void test_task_ids_are_monotonically_increasing(void)
{
        struct task_t *a = task_create(dummy_func, NULL, PRIORITY_LOW);
        struct task_t *b = task_create(dummy_func, NULL, PRIORITY_LOW);

        TEST_ASSERT_GREATER_THAN(a->task_id, b->task_id);

        task_destroy(a);
        task_destroy(b);
}


void test_task_create_null_func_returns_null(void)
{
        struct task_t *t = task_create(NULL, NULL, PRIORITY_LOW);
        TEST_ASSERT_NULL(t);
}

void test_priority_str_low(void)
{
        TEST_ASSERT_EQUAL_STRING("LOW", task_priority_str(PRIORITY_LOW));
}

void test_priority_str_med(void)
{
        TEST_ASSERT_EQUAL_STRING("MEDIUM", task_priority_str(PRIORITY_MEDIUM));
}

void test_priority_str_high(void)
{
        TEST_ASSERT_EQUAL_STRING("HIGH", task_priority_str(PRIORITY_HIGH));
}

int main(void)
{
        UNITY_BEGIN();

        /* task_create */
        RUN_TEST(test_task_create_returns_non_null);
        RUN_TEST(test_task_create_sets_func);
        RUN_TEST(test_task_create_sets_arg);
        RUN_TEST(test_task_create_null_arg_allowed);
        RUN_TEST(test_task_create_sets_priority_low);
        RUN_TEST(test_task_create_sets_priority_med);
        RUN_TEST(test_task_create_sets_priority_high);
        RUN_TEST(test_task_create_next_is_null);

        /* task_id */
        RUN_TEST(test_task_ids_are_unique);
        RUN_TEST(test_task_ids_are_monotonically_increasing);

        /* null func */
        RUN_TEST(test_task_create_null_func_returns_null);

        /* priority_str */
        RUN_TEST(test_priority_str_low);
        RUN_TEST(test_priority_str_med);
        RUN_TEST(test_priority_str_high);

        return UNITY_END();
}