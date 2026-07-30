struct atomic_parameter_member_payload {
  unsigned int value;
};

int function_parameter_atomic_pointer_record_member(
    _Atomic(struct atomic_parameter_member_payload *) pointer) {
  return (int)pointer->value;
}
