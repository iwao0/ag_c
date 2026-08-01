// A callback factory must preserve qualifiers nested inside a
// prototype-scope VLA parameter in its external data signature.
struct global_vla_factory_const_cell {
  _Alignas(64) unsigned long long value;
};

typedef unsigned long long global_vla_factory_const_callback_t(
    int rows, int columns,
    const struct global_vla_factory_const_cell
        input[static restrict 1][*]);
typedef global_vla_factory_const_callback_t
    *global_vla_factory_const_factory_t(void);

extern global_vla_factory_const_factory_t
    *global_vla_callback_factory_element_const_mismatch;

int main(void) {
  struct global_vla_factory_const_cell input[1][1] = {{{42}}};
  global_vla_factory_const_callback_t *callback =
      global_vla_callback_factory_element_const_mismatch();
  return callback(1, 1, input) == 42ULL ? 0 : 1;
}
