#include <stdatomic.h>

// Compare-exchange may update expected, so its pointee cannot be const.
int main(void) {
  atomic_int value = 1;
  const int expected = 1;
  return atomic_compare_exchange_strong(&value, &expected, 2);
}
