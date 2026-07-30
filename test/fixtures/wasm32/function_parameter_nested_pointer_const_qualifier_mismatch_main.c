int function_parameter_nested_pointer_const_qualifier(
    const int *const *values);

int main(void) {
  static const int value = 42;
  const int *const pointer = &value;
  return function_parameter_nested_pointer_const_qualifier(
      &pointer);
}
