// Cross-TU nested Atomic union and complex callback parameters.
// Expected with nested_atomic_union_complex_callback_parameter_xtu_other.c: exit=42.
#include <stdatomic.h>

#include "test/fixtures/probes_found_bugs/nested_atomic_union_complex_callback_parameter_xtu.h"

static unsigned int sum_inspected_union(union words3 value) {
  return value.words[0] + value.words[1] + value.words[2];
}

union inspected_complex_parts {
  double _Complex value;
  double parts[2];
};

union inspected_float_complex_parts {
  float _Complex value;
  float parts[2];
};

static double _Complex make_inspected_complex(
    double real, double imaginary) {
  union inspected_complex_parts representation = {
      .parts = {real, imaginary},
  };
  return representation.value;
}

static float _Complex make_inspected_float_complex(
    float real, float imaginary) {
  union inspected_float_complex_parts representation = {
      .parts = {real, imaginary},
  };
  return representation.value;
}

unsigned int inspect_atomic_small_union(
    _Atomic(union word1) value) {
  union word1 snapshot = atomic_load(&value);
  union word1 replacement;

  atomic_store(&value, ((union word1){.bits = 6u}));
  replacement = atomic_load(&value);
  if (replacement.bits != 6u) {
    return 0;
  }
  return snapshot.bits;
}

unsigned int inspect_atomic_union(
    _Atomic(union words3) value) {
  union words3 snapshot = atomic_load(&value);

  atomic_store(&value, ((union words3){.words = {1, 2, 3}}));
  if (sum_inspected_union(atomic_load(&value)) != 6) {
    return 0;
  }
  return sum_inspected_union(snapshot);
}

unsigned int inspect_atomic_complex(
    _Atomic(double _Complex) value) {
  double _Complex snapshot = atomic_load(&value);
  double _Complex replacement = make_inspected_complex(1.5, 4.5);

  atomic_store(&value, replacement);
  replacement = atomic_load(&value);
  if (replacement != make_inspected_complex(1.5, 4.5)) {
    return 0;
  }
  return snapshot == make_inspected_complex(17.5, 24.5) ? 42u : 0u;
}

unsigned int inspect_atomic_float_complex(
    _Atomic(float _Complex) value) {
  float _Complex snapshot = atomic_load(&value);
  float _Complex replacement =
      make_inspected_float_complex(1.5f, 4.5f);

  atomic_store(&value, replacement);
  replacement = atomic_load(&value);
  if (replacement != make_inspected_float_complex(1.5f, 4.5f)) {
    return 0;
  }
  return snapshot == make_inspected_float_complex(17.5f, 24.5f)
             ? 42u
             : 0u;
}

int main(void) {
  unsigned int small_union_result =
      consume_atomic_small_union(inspect_atomic_small_union);
  unsigned int union_result =
      consume_atomic_union(inspect_atomic_union);
  unsigned int float_complex_result =
      consume_atomic_float_complex(inspect_atomic_float_complex);
  unsigned int complex_result =
      consume_atomic_complex(inspect_atomic_complex);

  return small_union_result == 42 &&
                 union_result == 42 &&
                 float_complex_result == 42 &&
                 complex_result == 42
             ? 42
             : 0;
}
