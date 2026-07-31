#include <stdatomic.h>

// atomic_load returns a non-lvalue result and cannot be assigned through.
int main(void) {
  atomic_int value = 1;
  atomic_load(&value) = 2;
  return value;
}
