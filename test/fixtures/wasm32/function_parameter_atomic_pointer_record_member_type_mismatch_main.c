struct atomic_parameter_member_payload {
  int value;
};

int function_parameter_atomic_pointer_record_member(
    _Atomic(struct atomic_parameter_member_payload *) pointer);

int main(void) {
  struct atomic_parameter_member_payload value = {42};
  _Atomic(struct atomic_parameter_member_payload *) pointer =
      &value;
  return function_parameter_atomic_pointer_record_member(pointer);
}
