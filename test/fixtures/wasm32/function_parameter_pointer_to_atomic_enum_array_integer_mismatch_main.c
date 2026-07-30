enum atomic_enum_element_array_unsigned {
  ATOMIC_ENUM_ELEMENT_ARRAY_ZERO = 0,
  ATOMIC_ENUM_ELEMENT_ARRAY_VALUE = 42
};

int read_pointer_to_atomic_enum_array(
    _Atomic(enum atomic_enum_element_array_unsigned) (*row)[2]);

int main(void) {
  _Atomic(enum atomic_enum_element_array_unsigned) row[2] = {
      ATOMIC_ENUM_ELEMENT_ARRAY_ZERO,
      ATOMIC_ENUM_ELEMENT_ARRAY_VALUE};
  return read_pointer_to_atomic_enum_array(&row);
}
