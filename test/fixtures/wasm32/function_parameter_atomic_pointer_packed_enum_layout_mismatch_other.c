// Paired with function_parameter_atomic_pointer_packed_enum_layout_mismatch_main.c.

struct atomic_packed_layout_payload {
  char tag;
  int value;
};

int read_atomic_pointer_packed_enum_layout(
    _Atomic(struct atomic_packed_layout_payload *) value) {
  return value->value;
}
