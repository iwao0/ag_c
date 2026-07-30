static int plain_value = 20;
static _Atomic(int) atomic_value = 22;

_Atomic(int *) make_atomic_pointer(void) {
  return &plain_value;
}

_Atomic(int) *make_atomic_pointee_pointer(void) {
  return &atomic_value;
}
