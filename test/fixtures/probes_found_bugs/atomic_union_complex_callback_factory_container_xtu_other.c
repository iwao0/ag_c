// Definitions for Atomic callback factory container data.
#include <stdatomic.h>

#include "test/fixtures/probes_found_bugs/nested_atomic_union_complex_callback_factory_xtu.h"

#ifndef AG_C_ATOMIC_UNION_COMPLEX_CALLBACK_FACTORY_CONTAINER_XTU_TYPES
#define AG_C_ATOMIC_UNION_COMPLEX_CALLBACK_FACTORY_CONTAINER_XTU_TYPES
struct atomic_factory_container {
  _Atomic(atomic_union_factory_function *) union_member;
  _Atomic(atomic_complex_factory_function *) complex_member;
};
#endif

union produced_factory_container_complex_parts {
  double _Complex value;
  double parts[2];
};

static double _Complex make_produced_factory_container_complex(
    double real, double imaginary) {
  union produced_factory_container_complex_parts representation = {
      .parts = {real, imaginary},
  };
  return representation.value;
}

static unsigned int inspect_container_atomic_union(
    _Atomic(union factory_words3) value) {
  union factory_words3 snapshot = atomic_load(&value);
  return snapshot.words[0] + snapshot.words[1] + snapshot.words[2];
}

static atomic_union_target_function *make_container_union_target(void) {
  return inspect_container_atomic_union;
}

static _Atomic(double _Complex) produce_container_atomic_complex(void) {
  return make_produced_factory_container_complex(17.5, 24.5);
}

static atomic_complex_target_function *make_container_complex_target(void) {
  return produce_container_atomic_complex;
}

struct atomic_factory_container shared_atomic_factory_container = {
    make_container_union_target,
    make_container_complex_target,
};

_Atomic(atomic_union_factory_function *)
    shared_atomic_union_factories[2] = {
        make_container_union_target,
        make_container_union_target,
};

_Atomic(atomic_complex_factory_function *)
    shared_atomic_complex_factories[2] = {
        make_container_complex_target,
        make_container_complex_target,
};
