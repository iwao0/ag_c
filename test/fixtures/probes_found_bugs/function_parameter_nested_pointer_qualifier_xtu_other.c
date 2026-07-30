int function_parameter_nested_pointer_qualifier(
    const int *const *values) {
  return **values;
}
