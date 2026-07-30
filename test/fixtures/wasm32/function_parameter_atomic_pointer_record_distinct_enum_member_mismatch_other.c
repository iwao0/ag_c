// Paired with function_parameter_atomic_pointer_record_distinct_enum_member_mismatch_main.c.

enum atomic_record_parameter_actual_enum {
  ATOMIC_RECORD_PARAMETER_ACTUAL_ZERO = 0,
  ATOMIC_RECORD_PARAMETER_ACTUAL_VALUE = 42
};

struct atomic_record_parameter_payload {
  enum atomic_record_parameter_actual_enum value;
};

int read_atomic_pointer_record_enum_member(
    _Atomic(struct atomic_record_parameter_payload *) value) {
  return value->value;
}
