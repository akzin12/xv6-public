#include "types.h"
#include "user.h"

int counter = 0;

void worker(void *arg) {
  int i;
  for(i = 0; i < 1000; i++) {
    ulock_acquire((struct ulock*)arg);
    counter++;
    ulock_release((struct ulock*)arg);
  }
}

int main(void) {
  struct ulock lock;
  ulockinit(&lock);
  for (int i = 0; i < 10; i++) {
    thread_create(worker, &lock);
  }
  for (int i = 0; i < 10; i++) {
    thread_join();
  }
  printf(1, "counter = %d\n", counter);
  exit();
}