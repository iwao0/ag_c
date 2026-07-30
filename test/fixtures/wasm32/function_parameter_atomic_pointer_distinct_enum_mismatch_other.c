// Paired with function_parameter_atomic_pointer_distinct_enum_mismatch_main.c.

enum atomic_pointer_actual_enum {
  ATOMIC_POINTER_ACTUAL_ZERO = 0,
  ATOMIC_POINTER_ACTUAL_VALUE = 42
};

int read_atomic_pointer_distinct_enum(
    _Atomic(enum atomic_pointer_actual_enum *) value) {
  return **&value;
}
