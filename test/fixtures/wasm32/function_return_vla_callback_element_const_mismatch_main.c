// A factory function's canonical result type must preserve qualifiers nested
// inside the returned callback's prototype-scope VLA parameter.
struct function_return_vla_const_cell {
  _Alignas(64) unsigned long long value;
};

typedef unsigned long long function_return_vla_const_callback_t(
    int rows, int columns,
    const struct function_return_vla_const_cell
        input[static restrict 1][*]);

function_return_vla_const_callback_t
    *function_return_vla_callback_element_const_mismatch(void);

int main(void) {
  struct function_return_vla_const_cell input[1][1] = {{{42}}};
  function_return_vla_const_callback_t *callback =
      function_return_vla_callback_element_const_mismatch();
  return callback(1, 1, input) == 42ULL ? 0 : 1;
}
