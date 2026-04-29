#include "types.h"
#include "user.h"

void ulockinit(struct ulock *lk) {
  lk->locked = 0;
}

void ulock_acquire(struct ulock *lk) {
  int old;
  do {
    asm volatile("lock; xchgl %0, %1" : "=r"(old), "+m"(lk->locked) : "0"(1) : "cc");
  } while (old);
}

void ulock_release(struct ulock *lk) {
  __sync_synchronize();
  lk->locked = 0;
}