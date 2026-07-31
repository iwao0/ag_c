#include <stdatomic.h>

struct order_value {
  int value;
};

// A thread-fence order cannot have aggregate type.
int main(void) {
  struct order_value order = {0};
  atomic_thread_fence(order);
  return 0;
}
