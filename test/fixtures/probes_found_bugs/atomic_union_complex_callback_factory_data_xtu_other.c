// Definitions for external Atomic callback factory pointers.
#include <stdatomic.h>

#include "test/fixtures/probes_found_bugs/nested_atomic_union_complex_callback_factory_xtu.h"

union factory_data_produced_complex_parts {
  double _Complex value;
  double parts[2];
};

static double _Complex make_factory_data_produced_complex(
    double real, double imaginary) {
  union factory_data_produced_complex_parts representation = {
      .parts = {real, imaginary},
  };
  return representation.value;
}

static unsigned int inspect_factory_data_atomic_union(
    _Atomic(union factory_words3) value) {
  union factory_words3 snapshot = atomic_load(&value);
  return snapshot.words[0] + snapshot.words[1] + snapshot.words[2];
}

static atomic_union_target_function *make_factory_data_union_target(void) {
  return inspect_factory_data_atomic_union;
}

static _Atomic(double _Complex)
produce_factory_data_atomic_complex(void) {
  return make_factory_data_produced_complex(17.5, 24.5);
}

static atomic_complex_target_function *
make_factory_data_complex_target(void) {
  return produce_factory_data_atomic_complex;
}

_Atomic(atomic_union_factory_function *)
    shared_atomic_union_factory =
        make_factory_data_union_target;
_Atomic(atomic_complex_factory_function *)
    shared_atomic_complex_factory =
        make_factory_data_complex_target;
