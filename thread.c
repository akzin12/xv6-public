#include "types.h"
#include "user.h"

#define STACK_SIZE 4096
#define NTHREADS 10

struct thread_args {
  void (*fn)(void*);
  void *arg;
};

static void thread_entry(void *ctx) {
  struct thread_args *ta = (struct thread_args*)ctx;
  ta->fn(ta->arg);
  exit();
}

int thread_create(void (*fn)(void*), void *arg) {
  char *stack = malloc(STACK_SIZE);
  if(stack == 0) return -1;

  // Store fn/arg at the bottom of the stack (well away from rsp which starts at top)
  struct thread_args *ta = (struct thread_args*)stack;
  ta->fn = fn;
  ta->arg = arg;

  int pid = clone(stack, STACK_SIZE, thread_entry, ta);
  if(pid < 0) {
    free(stack);
    return -1;
  }
  // printf(1, "this is thread_create: pid %d\n", pid);
  return pid;
}

int thread_join(void) {
  int pid = wait();
  if(pid < 0) return -1;
  // printf(1, "this is thread_join: joined pid %d\n", pid);
  return pid;
}
