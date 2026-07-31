// Atomic callback factory pointers as external data symbols.
// Expected with atomic_union_complex_callback_factory_data_xtu_other.c: exit=42.
#include <stdatomic.h>

#include "test/fixtures/probes_found_bugs/nested_atomic_union_complex_callback_factory_xtu.h"

extern _Atomic(atomic_union_factory_function *)
    shared_atomic_union_factory;
extern _Atomic(atomic_complex_factory_function *)
    shared_atomic_complex_factory;

struct guarded_factory_data_union {
  unsigned int before;
  _Atomic(union factory_words3) value;
  unsigned int after;
};

struct guarded_factory_data_complex {
  unsigned long long before;
  _Atomic(double _Complex) value;
  unsigned long long after;
};

union factory_data_consumed_complex_parts {
  double _Complex value;
  double parts[2];
};

static double _Complex make_factory_data_consumed_complex(
    double real, double imaginary) {
  union factory_data_consumed_complex_parts representation = {
      .parts = {real, imaginary},
  };
  return representation.value;
}

static unsigned int sum_factory_data_words(
    union factory_words3 value) {
  return value.words[0] + value.words[1] + value.words[2];
}

int main(void) {
  struct guarded_factory_data_union union_frame;
  struct guarded_factory_data_complex complex_frame;
  atomic_union_factory_function *union_factory =
      atomic_load(&shared_atomic_union_factory);
  atomic_complex_factory_function *complex_factory =
      atomic_load(&shared_atomic_complex_factory);
  atomic_union_target_function *union_target = union_factory();
  atomic_complex_target_function *complex_target = complex_factory();
  union factory_words3 union_snapshot;
  double _Complex complex_expected =
      make_factory_data_consumed_complex(17.5, 24.5);
  double _Complex complex_snapshot;
  unsigned int union_result;

  union_frame.before = 0x11223344u;
  union_frame.after = 0x55667788u;
  atomic_init(
      &union_frame.value,
      ((union factory_words3){.words = {17, 13, 12}}));
  union_result = union_target(union_frame.value);
  union_snapshot = atomic_load(&union_frame.value);

  complex_frame.before = 0x1122334455667788ULL;
  complex_frame.after = 0x8877665544332211ULL;
  atomic_init(
      &complex_frame.value,
      make_factory_data_consumed_complex(1.5, 4.5));
  complex_frame.value = complex_target();
  complex_snapshot = atomic_load(&complex_frame.value);

  return union_frame.before == 0x11223344u &&
                 union_frame.after == 0x55667788u &&
                 sum_factory_data_words(union_snapshot) == 42 &&
                 union_result == 42 &&
                 complex_frame.before == 0x1122334455667788ULL &&
                 complex_frame.after == 0x8877665544332211ULL &&
                 complex_snapshot == complex_expected
             ? 42
             : 0;
}
