// Cross-TU nested callback parameter with padded Atomic storage.
// Expected with nested_atomic_aggregate_callback_parameter_xtu_other.c: exit=42.
#include <stdatomic.h>

#include "test/fixtures/probes_found_bugs/nested_atomic_aggregate_callback_parameter_xtu.h"

static unsigned int sum_inspected_words(struct words3 value) {
  return value.first + value.second + value.third;
}

unsigned int inspect_atomic_words(
    _Atomic(struct words3) value) {
  struct words3 snapshot = atomic_load(&value);

  atomic_store(&value, ((struct words3){1, 2, 3}));
  if (sum_inspected_words(atomic_load(&value)) != 6) {
    return 0;
  }
  return sum_inspected_words(snapshot);
}

int main(void) {
  return (int)consume_atomic_words(inspect_atomic_words);
}
