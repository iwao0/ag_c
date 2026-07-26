// Cross-TU atomic aggregate callback relocation and ABI boundary.
// Expected with atomic_aggregate_callback_xtu_other.c: exit=42.
#include "test/fixtures/probes_found_bugs/atomic_aggregate_callback_xtu.h"

static callback_t *callbacks[2] = {
    rotate_words,
    rotate_words,
};

int main(void) {
  struct words3 result =
      callbacks[1]((struct words3){40, 1, 2});
  if (result.a != 1 || result.b != 2 || result.c != 40) {
    return 1;
  }
  return 42;
}
