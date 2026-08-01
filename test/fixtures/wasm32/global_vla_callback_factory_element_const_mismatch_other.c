// Incompatible definition: the factory returns a callback whose nested VLA
// element is mutable rather than const-qualified.
struct global_vla_factory_const_cell {
  _Alignas(64) unsigned long long value;
};

typedef unsigned long long global_vla_factory_mutable_callback_t(
    int rows, int columns,
    struct global_vla_factory_const_cell input[static restrict 1][*]);
typedef global_vla_factory_mutable_callback_t
    *global_vla_factory_mutable_factory_t(void);

static unsigned long long read_mutable_vla_factory_callback(
    int rows, int columns,
    struct global_vla_factory_const_cell (*input)[columns]) {
  unsigned long long total = 0;
  for (int row = 0; row < rows; row++) {
    for (int column = 0; column < columns; column++)
      total += input[row][column].value;
  }
  return total;
}

static global_vla_factory_mutable_callback_t
    *make_mutable_vla_factory_callback(void) {
  return read_mutable_vla_factory_callback;
}

global_vla_factory_mutable_factory_t
    *global_vla_callback_factory_element_const_mismatch =
        make_mutable_vla_factory_callback;
