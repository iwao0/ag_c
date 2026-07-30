// Paired with atomic_enum_pointer_to_array_signature_xtu_main.c.

_Atomic(int (*)[2])
roundtrip_atomic_enum_row_pointer(
    _Atomic(int (*)[2]) row) {
  return row;
}

int read_atomic_enum_element_row(
    _Atomic(unsigned int) (*row)[2]) {
  return (*row)[1];
}
