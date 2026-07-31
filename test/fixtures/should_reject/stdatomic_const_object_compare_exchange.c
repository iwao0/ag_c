#include <stdatomic.h>

// Compare-exchange may update its atomic object and therefore requires mutability.
int main(void) {
  const atomic_int value = 1;
  int expected = 1;
  return atomic_compare_exchange_strong(&value, &expected, 2);
}
