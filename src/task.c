#define _POSIX_C_SOURCE 200809L
#include "task.h"
#include <stdio.h>
#include <stdatomic.h>
#include <stdlib.h>

static atomic_uint_fast64_t g_task_id_count;

static const char *const g_task_priority_str[PRIORITY_COUNT] = {
        [PRIORITY_LOW] = "LOW",
        [PRIORITY_MEDIUM] = "MEDIUM",
        [PRIORITY_HIGH] = "HIGH",
};

struct task_t *task_create(void (*func)(void *arg), void *arg,
                enum task_priority priority)
{
        if (!func) {
                fprintf(stderr, "Can't create task with no function for worker\n");
                return NULL;
        }

        struct task_t *new_task = malloc(sizeof(*new_task));

        if (!new_task) {
                fprintf(stderr, "task_create: malloc out of memory\n");
                return NULL;
        }

        uint64_t new_id = atomic_fetch_add(&g_task_id_count, 1);

        new_task->task_id     = new_id;
        new_task->arg         = arg;
        new_task->func        = func;
        new_task->priority    = priority;
        new_task->submit_time.tv_sec = 0;
        new_task->submit_time.tv_nsec = 0;
        new_task->next = NULL;

        return new_task;
}

void task_destroy(struct task_t *task)
{
        free(task);
}

const char *task_priority_str(enum task_priority priority)
{
        if (priority < 0 || priority >= NUM_PRIORITIES)
                return "UNKNOWN";

        return g_task_priority_str[priority];
}
