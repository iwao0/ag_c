// Consumer definition TU for the nested Atomic aggregate callback fixture.
#include <stdatomic.h>

#include "test/fixtures/probes_found_bugs/nested_atomic_aggregate_callback_parameter_xtu.h"

struct guarded_atomic_words {
  unsigned int before;
  _Atomic(struct words3) value;
  unsigned int after;
};

static unsigned int sum_consumed_words(struct words3 value) {
  return value.first + value.second + value.third;
}

unsigned int consume_atomic_words(
    atomic_words_callback_function *callback) {
  struct guarded_atomic_words frame;
  struct words3 snapshot;
  unsigned int result;

  frame.before = 0x11223344u;
  frame.after = 0x55667788u;
  atomic_init(&frame.value, ((struct words3){17, 13, 12}));
  result = callback(frame.value);
  snapshot = atomic_load(&frame.value);

  if (frame.before != 0x11223344u ||
      frame.after != 0x55667788u ||
      sum_consumed_words(snapshot) != 42) {
    return 0;
  }
  return result;
}
