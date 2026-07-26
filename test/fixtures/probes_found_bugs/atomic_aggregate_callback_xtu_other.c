// Definition TU for the cross-TU atomic aggregate callback fixture.
#include "test/fixtures/probes_found_bugs/atomic_aggregate_callback_xtu.h"

struct words3 rotate_words(
    _Atomic(struct words3) value) {
  struct words3 snapshot = value;
  return (struct words3){
      snapshot.b, snapshot.c, snapshot.a};
}
