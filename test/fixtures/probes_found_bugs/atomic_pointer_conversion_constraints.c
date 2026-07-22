#include <assert.h>
#include <stdatomic.h>

static _Atomic int *atomic_identity(_Atomic int *pointer) {
  return pointer;
}

static void *to_void(_Atomic int *pointer) {
  return pointer;
}

static _Atomic int *from_void(void *pointer) {
  return pointer;
}

int main(void) {
  _Atomic int atomic_value = 7;
  _Atomic int *atomic_pointer = &atomic_value;
  _Atomic int *same_pointer = atomic_identity(atomic_pointer);
  assert(same_pointer == &atomic_value);
  assert(atomic_load(same_pointer) == 7);

  void *generic_pointer = to_void(same_pointer);
  _Atomic int *roundtrip_pointer = from_void(generic_pointer);
  assert(roundtrip_pointer == &atomic_value);
  atomic_store(roundtrip_pointer, 11);
  assert(atomic_load(&atomic_value) == 11);

  int plain_value = 13;
  _Atomic int *explicit_atomic_pointer =
      (_Atomic int *)&plain_value;
  int *explicit_plain_pointer = (int *)explicit_atomic_pointer;
  assert(explicit_plain_pointer == &plain_value);

  void *plain_generic_pointer = &plain_value;
  _Atomic int *generic_atomic_pointer = plain_generic_pointer;
  void *generic_roundtrip = generic_atomic_pointer;
  assert(generic_roundtrip == plain_generic_pointer);
  return 0;
}
