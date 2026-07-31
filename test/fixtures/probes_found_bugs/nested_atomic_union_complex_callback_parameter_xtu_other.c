// Consumer definition TU for nested Atomic union and complex callbacks.
#include <stdatomic.h>

#include "test/fixtures/probes_found_bugs/nested_atomic_union_complex_callback_parameter_xtu.h"

struct guarded_atomic_union {
  unsigned int before;
  _Atomic(union words3) value;
  unsigned int after;
};

struct guarded_atomic_small_union {
  unsigned int before;
  _Atomic(union word1) value;
  unsigned int after;
};

struct guarded_atomic_complex {
  unsigned long long before;
  _Atomic(double _Complex) value;
  unsigned long long after;
};

struct guarded_atomic_float_complex {
  unsigned long long before;
  _Atomic(float _Complex) value;
  unsigned long long after;
};

union consumed_complex_parts {
  double _Complex value;
  double parts[2];
};

union consumed_float_complex_parts {
  float _Complex value;
  float parts[2];
};

static double _Complex make_consumed_complex(
    double real, double imaginary) {
  union consumed_complex_parts representation = {
      .parts = {real, imaginary},
  };
  return representation.value;
}

static float _Complex make_consumed_float_complex(
    float real, float imaginary) {
  union consumed_float_complex_parts representation = {
      .parts = {real, imaginary},
  };
  return representation.value;
}

static unsigned int sum_consumed_union(union words3 value) {
  return value.words[0] + value.words[1] + value.words[2];
}

unsigned int consume_atomic_small_union(
    atomic_small_union_callback_function *callback) {
  struct guarded_atomic_small_union frame;
  union word1 snapshot;
  unsigned int result;

  frame.before = 0x11223344u;
  frame.after = 0x55667788u;
  atomic_init(&frame.value, ((union word1){.bits = 42u}));
  result = callback(frame.value);
  snapshot = atomic_load(&frame.value);

  if (frame.before != 0x11223344u ||
      frame.after != 0x55667788u ||
      snapshot.bits != 42u) {
    return 0;
  }
  return result;
}

unsigned int consume_atomic_union(
    atomic_union_callback_function *callback) {
  struct guarded_atomic_union frame;
  union words3 snapshot;
  unsigned int result;

  frame.before = 0x11223344u;
  frame.after = 0x55667788u;
  atomic_init(
      &frame.value,
      ((union words3){.words = {17, 13, 12}}));
  result = callback(frame.value);
  snapshot = atomic_load(&frame.value);

  if (frame.before != 0x11223344u ||
      frame.after != 0x55667788u ||
      sum_consumed_union(snapshot) != 42) {
    return 0;
  }
  return result;
}

unsigned int consume_atomic_complex(
    atomic_complex_callback_function *callback) {
  struct guarded_atomic_complex frame;
  double _Complex initial = make_consumed_complex(17.5, 24.5);
  double _Complex snapshot;
  unsigned int result;

  frame.before = 0x1122334455667788ULL;
  frame.after = 0x8877665544332211ULL;
  atomic_init(&frame.value, initial);
  result = callback(frame.value);
  snapshot = atomic_load(&frame.value);

  if (frame.before != 0x1122334455667788ULL ||
      frame.after != 0x8877665544332211ULL ||
      snapshot != initial) {
    return 0;
  }
  return result;
}

unsigned int consume_atomic_float_complex(
    atomic_float_complex_callback_function *callback) {
  struct guarded_atomic_float_complex frame;
  float _Complex initial =
      make_consumed_float_complex(17.5f, 24.5f);
  float _Complex snapshot;
  unsigned int result;

  frame.before = 0x1122334455667788ULL;
  frame.after = 0x8877665544332211ULL;
  atomic_init(&frame.value, initial);
  result = callback(frame.value);
  snapshot = atomic_load(&frame.value);

  if (frame.before != 0x1122334455667788ULL ||
      frame.after != 0x8877665544332211ULL ||
      snapshot != initial) {
    return 0;
  }
  return result;
}
