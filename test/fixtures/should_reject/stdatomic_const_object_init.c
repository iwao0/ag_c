#include <stdatomic.h>

// atomic_init writes the supplied value and cannot reinitialize a const object.
int main(void) {
  const atomic_int value = 1;
  atomic_init(&value, 2);
  return 0;
}
