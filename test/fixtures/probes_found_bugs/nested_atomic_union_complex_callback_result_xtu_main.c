// Cross-TU nested Atomic union and complex callback results.
// Expected with nested_atomic_union_complex_callback_result_xtu_other.c: exit=42.
#include <stdatomic.h>

#include "test/fixtures/probes_found_bugs/nested_atomic_union_complex_callback_result_xtu.h"

union produced_complex_parts {
  double _Complex value;
  double parts[2];
};

union produced_float_complex_parts {
  float _Complex value;
  float parts[2];
};

static double _Complex make_produced_complex(
    double real, double imaginary) {
  union produced_complex_parts representation = {
      .parts = {real, imaginary},
  };
  return representation.value;
}

static float _Complex make_produced_float_complex(
    float real, float imaginary) {
  union produced_float_complex_parts representation = {
      .parts = {real, imaginary},
  };
  return representation.value;
}

static _Atomic(union result_word1)
produce_atomic_small_union(void) {
  return (union result_word1){.bits = 42u};
}

static _Atomic(union result_words3) produce_atomic_union(void) {
  return (union result_words3){.words = {17, 13, 12}};
}

static _Atomic(double _Complex) produce_atomic_complex(void) {
  return make_produced_complex(17.5, 24.5);
}

static _Atomic(float _Complex) produce_atomic_float_complex(void) {
  return make_produced_float_complex(17.5f, 24.5f);
}

int main(void) {
  unsigned int small_union_result =
      consume_atomic_small_union_result(produce_atomic_small_union);
  unsigned int union_result =
      consume_atomic_union_result(produce_atomic_union);
  unsigned int float_complex_result =
      consume_atomic_float_complex_result(produce_atomic_float_complex);
  unsigned int complex_result =
      consume_atomic_complex_result(produce_atomic_complex);

  return small_union_result == 42 &&
                 union_result == 42 &&
                 float_complex_result == 42 &&
                 complex_result == 42
             ? 42
             : 0;
}
