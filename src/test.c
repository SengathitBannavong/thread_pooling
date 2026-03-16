#include <stdio.h>
#include <stdlib.h>
#include "task.h"

void anything(void *arg) {
  (void)arg;
  printf("hi\n");
}

int main() {
  struct task_t *new = NULL; // task_create(anything, NULL, PRIORITY_LOW);
  if(new)
    task_destroy(new);
  else printf("[ERROR] create task not completed.   %s:%d\n",__FILE__,__LINE__);
  return 0;
}