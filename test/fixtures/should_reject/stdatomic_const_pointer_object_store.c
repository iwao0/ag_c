#include <stdatomic.h>

// Const qualification applies to an atomic pointer object as well as integer atomics.
int main(void) {
  int target = 1;
  const _Atomic(int *) pointer = 0;
  atomic_store(&pointer, &target);
  return 0;
}
