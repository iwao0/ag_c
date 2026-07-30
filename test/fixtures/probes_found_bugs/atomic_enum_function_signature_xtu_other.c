// Paired with atomic_enum_function_signature_xtu_main.c.

_Atomic(int *)
roundtrip_atomic_enum_pointer(_Atomic(int *) value) {
  return value;
}

int read_atomic_enum_pointee(
    _Atomic(unsigned int) *value) {
  return *value;
}
