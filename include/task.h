#ifndef TASK_H
#define TASK_H

#include <stdint.h>
#include <time.h>

#define TASK_PRIORITY_LIST(X) \
  X(PRIORITY_LOW,    "LOW") \
  X(PRIORITY_MEDIUM, "MEDIUM") \
  X(PRIORITY_HIGH,   "HIGH")

enum task_priority {
  #define TASK_PRIORITY_ENUM(name, label) name,
  TASK_PRIORITY_LIST(TASK_PRIORITY_ENUM)
  #undef TASK_PRIORITY_ENUM
  PRIORITY_COUNT
};

#define NUM_PRIORITIES PRIORITY_COUNT

struct task_t {
  uint64_t            task_id;            /* unique ID */
  void                (*func)(void *arg); /* function for worker calling */
  void                *arg;               /* pointer to argument */
  enum task_priority  priority;
  struct timespec     submit_time;
  struct task_t       *next;
};

struct task_t   *task_create(void (*func)(void *arg), void *arg,
                             enum task_priority priority);
void            task_destroy(struct task_t *task);
const char      *task_priority_str(enum task_priority priority);


#endif