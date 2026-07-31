// Atomic callback factory pointers in external record members and arrays.
// Expected with atomic_union_complex_callback_factory_container_xtu_other.c: exit=42.
#include <stdatomic.h>

#include "test/fixtures/probes_found_bugs/nested_atomic_union_complex_callback_factory_xtu.h"

#ifndef AG_C_ATOMIC_UNION_COMPLEX_CALLBACK_FACTORY_CONTAINER_XTU_TYPES
#define AG_C_ATOMIC_UNION_COMPLEX_CALLBACK_FACTORY_CONTAINER_XTU_TYPES
struct atomic_factory_container {
  _Atomic(atomic_union_factory_function *) union_member;
  _Atomic(atomic_complex_factory_function *) complex_member;
};
#endif

extern struct atomic_factory_container shared_atomic_factory_container;
extern _Atomic(atomic_union_factory_function *)
    shared_atomic_union_factories[2];
extern _Atomic(atomic_complex_factory_function *)
    shared_atomic_complex_factories[2];

union factory_container_complex_parts {
  double _Complex value;
  double parts[2];
};

static double _Complex make_factory_container_complex(
    double real, double imaginary) {
  union factory_container_complex_parts representation = {
      .parts = {real, imaginary},
  };
  return representation.value;
}

static unsigned int run_container_union_factory(
    atomic_union_factory_function *factory) {
  atomic_union_target_function *target = factory();
  _Atomic(union factory_words3) value =
      (union factory_words3){.words = {17, 13, 12}};
  union factory_words3 snapshot;
  unsigned int result = target(value);

  snapshot = atomic_load(&value);
  return snapshot.words[0] + snapshot.words[1] + snapshot.words[2] == 42 &&
                 result == 42
             ? 42u
             : 0u;
}

static unsigned int run_container_complex_factory(
    atomic_complex_factory_function *factory) {
  atomic_complex_target_function *target = factory();
  _Atomic(double _Complex) value = target();
  double _Complex snapshot = atomic_load(&value);

  return snapshot == make_factory_container_complex(17.5, 24.5)
             ? 42u
             : 0u;
}

int main(void) {
  atomic_union_factory_function *member_union_factory =
      atomic_load(&shared_atomic_factory_container.union_member);
  atomic_complex_factory_function *member_complex_factory =
      atomic_load(&shared_atomic_factory_container.complex_member);
  atomic_union_factory_function *array_union_factory =
      atomic_load(&shared_atomic_union_factories[1]);
  atomic_complex_factory_function *array_complex_factory =
      atomic_load(&shared_atomic_complex_factories[1]);

  return run_container_union_factory(member_union_factory) == 42 &&
                 run_container_complex_factory(member_complex_factory) == 42 &&
                 run_container_union_factory(array_union_factory) == 42 &&
                 run_container_complex_factory(array_complex_factory) == 42
             ? 42
             : 0;
}
