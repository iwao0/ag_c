#include <stdatomic.h>

enum first_state {
  FIRST_STATE = 1
};

enum second_state {
  SECOND_STATE = 1
};

// Distinct enum types are not compatible expected/object value types.
int main(void) {
  _Atomic(enum first_state) value = FIRST_STATE;
  enum second_state expected = SECOND_STATE;
  return atomic_compare_exchange_strong(
      &value, &expected, FIRST_STATE);
}
