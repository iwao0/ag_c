#include <stdatomic.h>

struct order_value {
  int value;
};

// A memory_order argument must be arithmetic and cannot be an aggregate.
int main(void) {
  atomic_int value = 1;
  struct order_value order = {0};
  atomic_store_explicit(&value, 2, order);
  return 0;
}
