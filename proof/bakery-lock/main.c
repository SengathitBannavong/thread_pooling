#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <pthread.h>

#define N_THREAD 3
#define ITER 15

volatile bool flag[N_THREAD];
volatile int label[N_THREAD];
pthread_t t[N_THREAD];
volatile int count = 0;
volatile int shutdown = 0;

struct thread_id {
  int id;
};

struct thread_id t_id[N_THREAD];

static int max_label() {
  int max = -1;
  for(int i = 0; i < N_THREAD; i++) {
    if(label[i] > max) max = label[i];
  }
  return max;
}


static int check_k_i(int k, int i) {
  if(label[k] < label[i]) return 1;

  if(label[k] == label[i] && k < i) return 1;

  return 0;
}

static int check_waiting(int id) {
  for(int k = 0 ; k < N_THREAD; k++) {
    if(k == id) continue;
    if(flag[k] && check_k_i(k,id)) return 1;
  }
  return 0;
}

void *routine(void *arg) {
  int id = ((struct thread_id*)arg)->id;
  do {
    // lock
    flag[id] = true;
    label[id] = max_label() + 1;
    printf("thread #%d label #%d is waiting\n", id, label[id]);
    while(check_waiting(id)) {} // waiting
    printf("thread #%d is done waiting\n", id);

    /* --- start critical section --- */
    printf("thread #%d on critical section\n", id);
    int temp = count;
    count = temp +1;
    if(count > ITER) shutdown = 1;
    printf("thread #%d done critical section\n", id);
    /* --- end critical section --- */
  
    // unlock
    printf("thread #%d is unlocking\n", id);
    flag[id] = false;
  }while(!shutdown);

  return NULL;
}

// try bakery lock
int main() {
  for(int i = 0; i < N_THREAD; i++) {
    flag[i] = false;
    label[i] = 0;
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