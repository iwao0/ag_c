#include <stdatomic.h>

// atomic_thread_fence has void type and cannot initialize an object.
int main(void) {
  int value = atomic_thread_fence(memory_order_seq_cst);
  return value;
}
