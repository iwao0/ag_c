enum atomic_enum_array_pointer_expected {
  ATOMIC_ENUM_ARRAY_POINTER_EXPECTED_ZERO = 0,
  ATOMIC_ENUM_ARRAY_POINTER_EXPECTED_VALUE = 42
};

int read_atomic_pointer_to_enum_array(
    _Atomic(enum atomic_enum_array_pointer_expected (*)[2]) row);

int main(void) {
  enum atomic_enum_array_pointer_expected row[2] = {
      ATOMIC_ENUM_ARRAY_POINTER_EXPECTED_ZERO,
      ATOMIC_ENUM_ARRAY_POINTER_EXPECTED_VALUE};
  return read_atomic_pointer_to_enum_array(&row);
}
