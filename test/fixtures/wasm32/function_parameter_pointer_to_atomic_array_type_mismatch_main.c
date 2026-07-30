typedef _Atomic(int) atomic_element_row_t[2];

int function_parameter_pointer_to_atomic_array(
    atomic_element_row_t *row);

int main(void) {
  atomic_element_row_t row = {42, 0};
  return function_parameter_pointer_to_atomic_array(&row);
}
