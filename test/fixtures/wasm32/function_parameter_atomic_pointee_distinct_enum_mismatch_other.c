// Paired with function_parameter_atomic_pointee_distinct_enum_mismatch_main.c.

enum atomic_pointee_actual_enum {
  ATOMIC_POINTEE_ACTUAL_ZERO = 0,
  ATOMIC_POINTEE_ACTUAL_VALUE = 42
};

int read_atomic_pointee_distinct_enum(
    _Atomic(enum atomic_pointee_actual_enum) *value) {
  return *value;
}
