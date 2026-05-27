#include "unity.h"
#include "threadpool.h"
#include <unistd.h>
#include <stdlib.h>
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
        for (int retry = 0; retry < 6; retry++) {
                atomic_store(&executed_count, 0);
                int num_tasks = 300;

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
/* Regression harness for the shutdown TOCTOU race closed in PR1 (bd88864).
 *
 * Pre-PR1, thread_pool_submit used release/acquire on `shutdown` and
 * `in_flight_submits`. That permitted an interleaving where the destroyer
 * observed in_flight==0 while a submitter had passed the shutdown check
 * but not yet incremented in_flight, leading to a use-after-free in pq_push
 * against a queue the destroyer had already freed.
 *
 * PR1 reordered submit to increment-then-check and promoted both atomics
 * to memory_order_seq_cst, giving a global total order that closes the gap.
 *
 * Two modes:
 *   - default (no env var): light smoke test, fits the make test per-test
 *     timeout. Does NOT reliably differentiate fixed from broken code on x86.
 *   - TP_RACE_STRESS=1: ~2000 trials, ~16 submitter threads, ~2 minutes.
 *     Pre-PR1 code hangs or crashes inside this within tens of seconds.
 *
 * To verify the test catches the regression (manual procedure):
 *   1. Weaken the two seq_cst ops in thread_pool.c:thread_pool_submit and
 *      thread_pool_destroy back to release/acquire and swap submit's
 *      check-then-increment order to increment-then-check.
 *   2. TP_RACE_STRESS=1 ./bin/test_deadlock
 *   3. Expect a hang or crash; restore the seq_cst ops; rerun and expect pass.
 */

/* Default (fast, smoke-test only — fits the 20s `make test` per-test budget).
 * For real regression-detection power, run with TP_RACE_STRESS=1 (or any non-zero
 * value); the test bumps to ~2000 trials × 16 threads, takes ~2 minutes, and
 * does reliably crash on pre-PR1 code. */
#define SUBMIT_RACE_TRIALS_FAST     15
#define SUBMIT_RACE_THREADS_FAST    8
#define SUBMIT_RACE_BURST_FAST      300

#define SUBMIT_RACE_TRIALS_STRESS   2000
#define SUBMIT_RACE_THREADS_STRESS  16
#define SUBMIT_RACE_BURST_STRESS    50

static int submit_race_trials;
static int submit_race_threads;
static int submit_race_burst;

static atomic_int observed_shutdown_returns;  /* sanity: race window actually exercised */

static void *race_submitter(void *arg) {
        thread_pool_t *pool = (thread_pool_t *)arg;
        for (int i = 0; i < submit_race_burst; i++) {
                int64_t id = thread_pool_submit(pool, simple_task, NULL, PRIORITY_LOW);
                if (id < 0) {
                        /* shutdown observed — pool may be freed after the next
                         * submit if the destroyer drains. Stop touching pool. */
                        atomic_fetch_add(&observed_shutdown_returns, 1);
                        break;
                }
        }
        return NULL;
}

void test_submit_vs_destroy_race(void) {
        const char *stress = getenv("TP_RACE_STRESS");
        if (stress && stress[0] && stress[0] != '0') {
                submit_race_trials  = SUBMIT_RACE_TRIALS_STRESS;
                submit_race_threads = SUBMIT_RACE_THREADS_STRESS;
                submit_race_burst   = SUBMIT_RACE_BURST_STRESS;
                printf("[INFO] stress mode: %d trials x %d threads x %d burst\n",
                       submit_race_trials, submit_race_threads, submit_race_burst);
        } else {
                submit_race_trials  = SUBMIT_RACE_TRIALS_FAST;
                submit_race_threads = SUBMIT_RACE_THREADS_FAST;
                submit_race_burst   = SUBMIT_RACE_BURST_FAST;
        }

        atomic_store(&observed_shutdown_returns, 0);

        pthread_t *subs = malloc(sizeof(pthread_t) * (size_t)submit_race_threads);
        TEST_ASSERT_NOT_NULL(subs);

        for (int trial = 0; trial < submit_race_trials; trial++) {
                thread_pool_t *pool = thread_pool_init(2);
                TEST_ASSERT_NOT_NULL(pool);

                for (int i = 0; i < submit_race_threads; i++) {
                        int rc = pthread_create(&subs[i], NULL, race_submitter, pool);
                        TEST_ASSERT_EQUAL_INT(0, rc);
                }

                /* No fixed sleep — just yield once so submitters can enter
                 * thread_pool_submit, then destroy while they are mid-flight.
                 * The whole point is to overlap destroy with active submitters. */
                sched_yield();

                thread_pool_destroy(&pool);
                /* After destroy returns, pool is freed. Submitters that already
                 * saw -1 have stopped; any submitter still inside submit() was
                 * blocked by destroy's in_flight spin and has already returned. */

                for (int i = 0; i < submit_race_threads; i++)
                        pthread_join(subs[i], NULL);
        }
        free(subs);

        /* Sanity check: if we never saw a single -1 return across all trials,
         * the test isn't actually exercising the overlap window and the
         * "no crash" pass is meaningless. We expect many -1 returns. */
        int shutdown_hits = atomic_load(&observed_shutdown_returns);
        printf("[INFO] race overlap exercised: %d submitter calls observed shutdown\n",
               shutdown_hits);
        TEST_ASSERT_GREATER_THAN_INT_MESSAGE(
                0, shutdown_hits,
                "no submitter ever saw shutdown — destroy/submit did not overlap, "
                "test is not actually exercising the race window");
}

int main(void) {
        UNITY_BEGIN();
        RUN_TEST(test_premature_drain);
        RUN_TEST(test_submit_vs_destroy_race);
        return UNITY_END();
}
