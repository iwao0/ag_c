#pragma pack(push, 1)
struct atomic_parameter_layout_payload {
  char tag;
  int value;
};
#pragma pack(pop)

int function_parameter_atomic_pointer_record_layout(
    _Atomic(struct atomic_parameter_layout_payload *) pointer);

int main(void) {
  struct atomic_parameter_layout_payload value = {'x', 42};
  _Atomic(struct atomic_parameter_layout_payload *) pointer =
      &value;
  return function_parameter_atomic_pointer_record_layout(pointer);
}
