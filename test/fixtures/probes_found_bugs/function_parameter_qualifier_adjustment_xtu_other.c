int function_parameter_qualifier_adjustment(
    int value, int *fixed_pointer, int *variable_pointer) {
  return value + *fixed_pointer + *variable_pointer;
}
