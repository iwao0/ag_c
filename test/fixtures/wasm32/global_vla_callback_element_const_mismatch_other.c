// Incompatible definition: the nested VLA element is not const-qualified.
struct global_vla_const_cell {
  _Alignas(64) unsigned long long value;
};

static unsigned long long read_mutable_vla_callback(
    int rows, int columns,
    struct global_vla_const_cell (*input)[columns]) {
  unsigned long long total = 0;
  for (int row = 0; row < rows; row++) {
    for (int column = 0; column < columns; column++)
      total += input[row][column].value;
  }
  return total;
}

unsigned long long (*global_vla_callback_element_const_mismatch)(
    int, int, struct global_vla_const_cell (*)[*]) =
    read_mutable_vla_callback;
