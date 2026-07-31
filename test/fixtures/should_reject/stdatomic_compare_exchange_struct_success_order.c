#include <stdatomic.h>

struct order_value {
  int value;
};

// The success order of compare-exchange cannot have aggregate type.
int main(void) {
  atomic_int value = 1;
  int expected = 1;
  struct order_value order = {0};
  return atomic_compare_exchange_strong_explicit(
      &value, &expected, 2, order, memory_order_relaxed);
}
