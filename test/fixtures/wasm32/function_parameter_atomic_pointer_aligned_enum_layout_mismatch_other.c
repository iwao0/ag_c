// Paired with function_parameter_atomic_pointer_aligned_enum_layout_mismatch_main.c.

struct atomic_aligned_layout_payload {
  char tag;
  _Alignas(4) unsigned int value;
};

int read_atomic_pointer_aligned_enum_layout(
    _Atomic(struct atomic_aligned_layout_payload *) value) {
  return (int)value->value;
}
