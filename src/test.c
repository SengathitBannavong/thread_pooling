#include "unity.h"
#include <stdio.h>
#include <stdlib.h>
#include "priority_queue.h"

struct priority_queue_t pq;

void dummy_func(void *arg) {
  (void)arg;
  int a = 1;
  a++;
  return;
}

void setUp(void) {
  pq_init(&pq);
}

void tearDown(void) {
  pq_destroy(&pq);
}

void test_push_pop_single(void) {
  struct task_t *t = task_create(dummy_func, NULL, PRIORITY_HIGH);
  pq_push(&pq, t);
  struct task_t *out = pq_pop_nonblock(&pq);
  TEST_ASSERT_NOT_NULL(out);
  TEST_ASSERT_EQUAL(PRIORITY_HIGH, out->priority);
  task_destroy(out);
}

void test_priority_order(void) {
  struct task_t *lo = task_create(dummy_func, NULL, PRIORITY_LOW);
  struct task_t *hi = task_create(dummy_func, NULL, PRIORITY_HIGH);
  struct task_t *md = task_create(dummy_func, NULL, PRIORITY_MEDIUM);

  pq_push(&pq,lo);
  pq_push(&pq,hi);
  pq_push(&pq,md);

  struct task_t *t1 = pq_pop_nonblock(&pq);
  struct task_t *t2 = pq_pop_nonblock(&pq);
  struct task_t *t3 = pq_pop_nonblock(&pq);

  TEST_ASSERT_EQUAL(PRIORITY_HIGH, t1->priority);
  TEST_ASSERT_EQUAL(PRIORITY_MEDIUM, t2->priority);
  TEST_ASSERT_EQUAL(PRIORITY_LOW, t3->priority);

  task_destroy(t1);
  task_destroy(t2);
  task_destroy(t3);
}

void test_empty_returns_null(void) {
  TEST_ASSERT_NULL(pq_pop_nonblock(&pq));
  TEST_ASSERT_EQUAL(1, pq_is_empty(&pq));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_push_pop_single);
  RUN_TEST(test_priority_order);
  RUN_TEST(test_empty_returns_null);
  return UNITY_END();
}