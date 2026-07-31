#include <stdatomic.h>

// Pointer atomic objects support add/subtract, but not bitwise fetch operations.
int main(void) {
  int values[2] = {0};
  _Atomic(int *) pointer = values;
  (void)atomic_fetch_or(&pointer, 1);
  return 0;
}
