// Consumer definition TU for nested Atomic union and complex callback results.
#include <stdatomic.h>

#include "test/fixtures/probes_found_bugs/nested_atomic_union_complex_callback_result_xtu.h"

struct guarded_atomic_union_result {
  unsigned int before;
  _Atomic(union result_words3) value;
  unsigned int after;
};

struct guarded_atomic_small_union_result {
  unsigned int before;
  _Atomic(union result_word1) value;
  unsigned int after;
};

struct guarded_atomic_complex_result {
  unsigned long long before;
  _Atomic(double _Complex) value;
  unsigned long long after;
};

struct guarded_atomic_float_complex_result {
  unsigned long long before;
  _Atomic(float _Complex) value;
  unsigned long long after;
};

union consumed_result_complex_parts {
  double _Complex value;
  double parts[2];
};

union consumed_result_float_complex_parts {
  float _Complex value;
  float parts[2];
};

static double _Complex make_consumed_result_complex(
    double real, double imaginary) {
  union consumed_result_complex_parts representation = {
      .parts = {real, imaginary},
  };
  return representation.value;
}

static float _Complex make_consumed_result_float_complex(
    float real, float imaginary) {
  union consumed_result_float_complex_parts representation = {
      .parts = {real, imaginary},
  };
  return representation.value;
}

static unsigned int sum_result_words(union result_words3 value) {
  return value.words[0] + value.words[1] + value.words[2];
}

unsigned int consume_atomic_small_union_result(
    atomic_small_union_result_callback_function *callback) {
  struct guarded_atomic_small_union_result frame;
  union result_word1 snapshot;
  union result_word1 replacement;

  frame.before = 0x11223344u;
  frame.after = 0x55667788u;
  atomic_init(&frame.value, ((union result_word1){.bits = 1u}));
  frame.value = callback();
  snapshot = atomic_load(&frame.value);

  if (frame.before != 0x11223344u ||
      frame.after != 0x55667788u ||
      snapshot.bits != 42u) {
    return 0;
  }
  atomic_store(&frame.value, ((union result_word1){.bits = 15u}));
  replacement = atomic_load(&frame.value);
  return replacement.bits == 15u ? 42u : 0u;
}

unsigned int consume_atomic_union_result(
    atomic_union_result_callback_function *callback) {
  struct guarded_atomic_union_result frame;
  union result_words3 snapshot;

  frame.before = 0x11223344u;
  frame.after = 0x55667788u;
  atomic_init(
      &frame.value,
      ((union result_words3){.words = {1, 2, 3}}));
  frame.value = callback();
  snapshot = atomic_load(&frame.value);

  if (frame.before != 0x11223344u ||
      frame.after != 0x55667788u ||
      sum_result_words(snapshot) != 42) {
    return 0;
  }
  atomic_store(
      &frame.value,
      ((union result_words3){.words = {4, 5, 6}}));
  return sum_result_words(atomic_load(&frame.value)) == 15 ? 42u : 0u;
}

unsigned int consume_atomic_complex_result(
    atomic_complex_result_callback_function *callback) {
  struct guarded_atomic_complex_result frame;
  double _Complex initial = make_consumed_result_complex(1.5, 4.5);
  double _Complex expected = make_consumed_result_complex(17.5, 24.5);
  double _Complex snapshot;

  frame.before = 0x1122334455667788ULL;
  frame.after = 0x8877665544332211ULL;
  atomic_init(&frame.value, initial);
  frame.value = callback();
  snapshot = atomic_load(&frame.value);

  if (frame.before != 0x1122334455667788ULL ||
      frame.after != 0x8877665544332211ULL ||
      snapshot != expected) {
    return 0;
  }
  atomic_store(&frame.value, initial);
  return atomic_load(&frame.value) == initial ? 42u : 0u;
}

unsigned int consume_atomic_float_complex_result(
    atomic_float_complex_result_callback_function *callback) {
  struct guarded_atomic_float_complex_result frame;
  float _Complex initial =
      make_consumed_result_float_complex(1.5f, 4.5f);
  float _Complex expected =
      make_consumed_result_float_complex(17.5f, 24.5f);
  float _Complex snapshot;

  frame.before = 0x1122334455667788ULL;
  frame.after = 0x8877665544332211ULL;
  atomic_init(&frame.value, initial);
  frame.value = callback();
  snapshot = atomic_load(&frame.value);

  if (frame.before != 0x1122334455667788ULL ||
      frame.after != 0x8877665544332211ULL ||
      snapshot != expected) {
    return 0;
  }
  atomic_store(&frame.value, initial);
  return atomic_load(&frame.value) == initial ? 42u : 0u;
}
