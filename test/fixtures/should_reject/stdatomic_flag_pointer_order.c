#include <stdatomic.h>

// An atomic-flag order cannot have pointer type.
int main(void) {
  atomic_flag flag = ATOMIC_FLAG_INIT;
  int order = 0;
  return atomic_flag_test_and_set_explicit(&flag, &order);
}
