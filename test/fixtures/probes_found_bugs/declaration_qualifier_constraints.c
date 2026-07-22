#include <assert.h>

typedef int *int_pointer;

_Atomic(int) _Atomic global_value = 0;

static int load_restricted(restrict int_pointer value) {
  return *value;
}

int main(void) {
  int local_value = 41;
  global_value = load_restricted(&local_value) + 1;
  assert(global_value == 42);
  return 0;
}
