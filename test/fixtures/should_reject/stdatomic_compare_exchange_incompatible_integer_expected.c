#include <stdatomic.h>

enum positive_state {
  POSITIVE_STATE_ZERO = 0,
  POSITIVE_STATE_ONE = 1
};

// A signed int is incompatible with this unsigned-compatible enum object.
int main(void) {
  _Atomic(enum positive_state) value = POSITIVE_STATE_ONE;
  int expected = 1;
  return atomic_compare_exchange_strong(
      &value, &expected, POSITIVE_STATE_ZERO);
}
