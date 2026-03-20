#include <stdio.h>
#include <string.h>
#include <pthread.h>

#define N_THREAD 8
#define ITER 100

volatile int level[N_THREAD];
volatile int victim[N_THREAD];
pthread_t t[N_THREAD];
volatile int count = 0;
volatile int shutdown = 0;

struct thread_id {
  int id;
};

struct thread_id t_id[N_THREAD];

static int check_k(int id, int i) {
  for(int k = 0; k < N_THREAD; k++) {
    if(k == id) continue;

    if(level[t_id[k].id] >= i) return 1;
  }
  return 0;
}

void *routine(void *arg) {
  int id = ((struct thread_id*)arg)->id;
  do {
    // lock
    for(int i = 1 ; i < N_THREAD; i++) {
      level[id] = i;
      victim[i] = id;
      printf("thread #%d is on level #%d\n", id, i);
      printf("thread #%d is waiting\n", id);
      int c;
      while( (c = check_k(id, i)) && victim[i] == id) {} // waiting
      printf("thread #%d is done waiting with check_k=%d and victim[%d]=%d \n", id, c, i, victim[i]);
    }

    /* --- start critical section --- */
    printf("thread #%d on critical section\n", id);
    int temp = count;
    count = temp +1;
    if(count > ITER) shutdown = 1;
    printf("thread #%d done critical section\n", id);
    /* --- end critical section --- */
  
    // unlock
    printf("thread #%d is unlocking\n", id);
    level[id] = 0;
  }while(!shutdown);
}

// try filter lock
int main() {
  for(int i = 0; i < N_THREAD; i++) {
    level[i] = 0;
  }

  for(int i = 0; i < N_THREAD; i++) {
    t_id[i].id = i;
    pthread_create(&t[i], NULL, routine, (void*)&t_id[i]);
  }

  for(int i = 0; i < N_THREAD; i++) {
    pthread_join(t[i], NULL);
  }
  
  return 0;
}