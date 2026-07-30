enum atomic_union_parameter_expected_enum {
  ATOMIC_UNION_PARAMETER_EXPECTED_ZERO = 0,
  ATOMIC_UNION_PARAMETER_EXPECTED_VALUE = 42
};

union atomic_union_parameter_payload {
  enum atomic_union_parameter_expected_enum value;
  int padding;
};

int read_atomic_pointer_union_enum_member(
    _Atomic(union atomic_union_parameter_payload *) value);

int main(void) {
  union atomic_union_parameter_payload value = {
      .value = ATOMIC_UNION_PARAMETER_EXPECTED_VALUE};
  return read_atomic_pointer_union_enum_member(&value);
}
