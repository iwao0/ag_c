// Paired with function_parameter_atomic_pointer_union_distinct_enum_member_mismatch_main.c.

enum atomic_union_parameter_actual_enum {
  ATOMIC_UNION_PARAMETER_ACTUAL_ZERO = 0,
  ATOMIC_UNION_PARAMETER_ACTUAL_VALUE = 42
};

union atomic_union_parameter_payload {
  int padding;
  enum atomic_union_parameter_actual_enum value;
};

int read_atomic_pointer_union_enum_member(
    _Atomic(union atomic_union_parameter_payload *) value) {
  return value->value;
}
