typedef int atomic_pointer_row_t[2];

int function_parameter_atomic_pointer_to_array(
    atomic_pointer_row_t *row) {
  return (*row)[0];
}
