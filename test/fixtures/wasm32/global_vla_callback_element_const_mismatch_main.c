// The element qualifier inside a prototype-scope VLA parameter remains part
// of an external callback object's canonical C type.
struct global_vla_const_cell {
  _Alignas(64) unsigned long long value;
};

typedef unsigned long long global_vla_const_callback_t(
    int rows, int columns,
    const struct global_vla_const_cell
        input[static restrict 1][*]);

extern global_vla_const_callback_t
    *global_vla_callback_element_const_mismatch;

int main(void) {
  struct global_vla_const_cell input[1][1] = {{{42}}};
  return global_vla_callback_element_const_mismatch(
             1, 1, input) == 42ULL
             ? 0
             : 1;
}
