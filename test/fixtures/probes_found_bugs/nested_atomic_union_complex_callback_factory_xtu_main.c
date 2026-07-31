// Cross-TU callback factories with nested Atomic union parameters and complex results.
// Expected with nested_atomic_union_complex_callback_factory_xtu_other.c: exit=42.
#include <stdatomic.h>

#include "test/fixtures/probes_found_bugs/nested_atomic_union_complex_callback_factory_xtu.h"

union factory_produced_complex_parts {
  double _Complex value;
  double parts[2];
};

static double _Complex make_factory_produced_complex(
    double real, double imaginary) {
  union factory_produced_complex_parts representation = {
      .parts = {real, imaginary},
  };
  return representation.value;
}

static unsigned int sum_factory_target_words(
    union factory_words3 value) {
  return value.words[0] + value.words[1] + value.words[2];
}

static unsigned int inspect_factory_atomic_union(
    _Atomic(union factory_words3) value) {
  union factory_words3 snapshot = atomic_load(&value);
  union factory_words3 replacement;

  atomic_store(
      &value,
      ((union factory_words3){.words = {1, 2, 3}}));
  replacement = atomic_load(&value);
  if (sum_factory_target_words(replacement) != 6) {
    return 0;
  }
  return sum_factory_target_words(snapshot);
}

static atomic_union_target_function *make_atomic_union_target(void) {
  return inspect_factory_atomic_union;
}

static _Atomic(double _Complex) produce_factory_atomic_complex(void) {
  return make_factory_produced_complex(17.5, 24.5);
}

static atomic_complex_target_function *make_atomic_complex_target(void) {
  return produce_factory_atomic_complex;
}

int main(void) {
  unsigned int union_result =
      invoke_atomic_union_factory(make_atomic_union_target);
  unsigned int complex_result =
      invoke_atomic_complex_factory(make_atomic_complex_target);

  return union_result == 42 && complex_result == 42 ? 42 : 0;
}
