#ifndef CPU_CORE_H
#define CPU_CORE_H

#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

int     get_num_core();
int     get_num_thread(); 

int get_num_core() {
  int cores = sysconf(_SC_NPROCESSORS_ONLN);
  return cores;
}

int get_num_thread() {
  FILE* f = fopen("/proc/sys/kernel/threads-max", "r");
  int max_threads;
  fscanf(f, "%d", &max_threads);
  fclose(f);

  return max_threads;
}

#endif