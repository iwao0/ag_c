// Paired with function_parameter_atomic_pointer_to_enum_array_distinct_mismatch_main.c.

enum atomic_enum_array_pointer_actual {
  ATOMIC_ENUM_ARRAY_POINTER_ACTUAL_ZERO = 0,
  ATOMIC_ENUM_ARRAY_POINTER_ACTUAL_VALUE = 42
};

int read_atomic_pointer_to_enum_array(
    _Atomic(enum atomic_enum_array_pointer_actual (*)[2]) row) {
  return (*row)[1];
}
