typedef int plain_element_row_t[2];

int function_parameter_pointer_to_atomic_array(
    plain_element_row_t *row) {
  return (*row)[0];
}
