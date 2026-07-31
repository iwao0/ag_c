#include <stdatomic.h>

// The failure order of compare-exchange cannot have pointer type.
int main(void) {
  atomic_int value = 1;
  int expected = 1;
  int order = 0;
  return atomic_compare_exchange_strong_explicit(
      &value, &expected, 2, memory_order_relaxed, &order);
}
