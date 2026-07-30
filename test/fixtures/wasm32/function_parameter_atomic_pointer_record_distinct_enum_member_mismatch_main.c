enum atomic_record_parameter_expected_enum {
  ATOMIC_RECORD_PARAMETER_EXPECTED_ZERO = 0,
  ATOMIC_RECORD_PARAMETER_EXPECTED_VALUE = 42
};

struct atomic_record_parameter_payload {
  enum atomic_record_parameter_expected_enum value;
};

int read_atomic_pointer_record_enum_member(
    _Atomic(struct atomic_record_parameter_payload *) value);

int main(void) {
  struct atomic_record_parameter_payload value = {
      ATOMIC_RECORD_PARAMETER_EXPECTED_VALUE};
  return read_atomic_pointer_record_enum_member(&value);
}
