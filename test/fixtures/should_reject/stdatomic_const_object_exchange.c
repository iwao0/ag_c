#include <stdatomic.h>

// atomic_exchange is a modifying operation even when its result is only read.
int main(void) {
  const atomic_int value = 1;
  return atomic_exchange(&value, 2);
}
