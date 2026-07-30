typedef int atomic_pointer_row_t[2];

int function_parameter_atomic_pointer_to_array(
    _Atomic(atomic_pointer_row_t *) row);

int main(void) {
  atomic_pointer_row_t row = {42, 0};
  _Atomic(atomic_pointer_row_t *) pointer = &row;
  return function_parameter_atomic_pointer_to_array(pointer);
}
