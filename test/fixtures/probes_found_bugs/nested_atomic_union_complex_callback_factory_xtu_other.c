// Consumer definition TU for nested Atomic callback factories.
#include <stdatomic.h>

#include "test/fixtures/probes_found_bugs/nested_atomic_union_complex_callback_factory_xtu.h"

struct guarded_factory_atomic_union {
  unsigned int before;
  _Atomic(union factory_words3) value;
  unsigned int after;
};

struct guarded_factory_atomic_complex {
  unsigned long long before;
  _Atomic(double _Complex) value;
  unsigned long long after;
};

union factory_consumed_complex_parts {
  double _Complex value;
  double parts[2];
};

static double _Complex make_factory_consumed_complex(
    double real, double imaginary) {
  union factory_consumed_complex_parts representation = {
      .parts = {real, imaginary},
  };
  return representation.value;
}

static unsigned int sum_factory_consumed_words(
    union factory_words3 value) {
  return value.words[0] + value.words[1] + value.words[2];
}

unsigned int invoke_atomic_union_factory(
    atomic_union_factory_function *factory) {
  struct guarded_factory_atomic_union frame;
  atomic_union_target_function *target = factory();
  union factory_words3 snapshot;
  unsigned int result;

  frame.before = 0x11223344u;
  frame.after = 0x55667788u;
  atomic_init(
      &frame.value,
      ((union factory_words3){.words = {17, 13, 12}}));
  result = target(frame.value);
  snapshot = atomic_load(&frame.value);

  if (frame.before != 0x11223344u ||
      frame.after != 0x55667788u ||
      sum_factory_consumed_words(snapshot) != 42) {
    return 0;
  }
  return result;
}

unsigned int invoke_atomic_complex_factory(
    atomic_complex_factory_function *factory) {
  struct guarded_factory_atomic_complex frame;
  atomic_complex_target_function *target = factory();
  double _Complex initial = make_factory_consumed_complex(1.5, 4.5);
  double _Complex expected = make_factory_consumed_complex(17.5, 24.5);
  double _Complex snapshot;

  frame.before = 0x1122334455667788ULL;
  frame.after = 0x8877665544332211ULL;
  atomic_init(&frame.value, initial);
  frame.value = target();
  snapshot = atomic_load(&frame.value);

  if (frame.before != 0x1122334455667788ULL ||
      frame.after != 0x8877665544332211ULL ||
      snapshot != expected) {
    return 0;
  }
  return 42u;
}
