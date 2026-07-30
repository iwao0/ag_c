// Paired with function_parameter_pointer_to_atomic_enum_array_integer_mismatch_main.c.

int read_pointer_to_atomic_enum_array(
    _Atomic(int) (*row)[2]) {
  return (*row)[1];
}
