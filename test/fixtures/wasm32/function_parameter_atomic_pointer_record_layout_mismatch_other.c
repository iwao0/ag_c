struct atomic_parameter_layout_payload {
  char tag;
  int value;
};

int function_parameter_atomic_pointer_record_layout(
    _Atomic(struct atomic_parameter_layout_payload *) pointer) {
  return pointer->value;
}
