// Incompatible definition: the returned callback's nested VLA element is
// mutable rather than const-qualified.
struct function_return_vla_const_cell {
  _Alignas(64) unsigned long long value;
};

typedef unsigned long long function_return_vla_mutable_callback_t(
    int rows, int columns,
    struct function_return_vla_const_cell input[static restrict 1][*]);

static unsigned long long read_mutable_function_return_vla_callback(
    int rows, int columns,
    struct function_return_vla_const_cell (*input)[columns]) {
  unsigned long long total = 0;
  for (int row = 0; row < rows; row++) {
    for (int column = 0; column < columns; column++)
      total += input[row][column].value;
  }
  return total;
}

function_return_vla_mutable_callback_t
    *function_return_vla_callback_element_const_mismatch(void) {
  return read_mutable_function_return_vla_callback;
}
