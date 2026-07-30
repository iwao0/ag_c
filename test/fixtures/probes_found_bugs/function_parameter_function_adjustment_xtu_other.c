int function_parameter_function_adjustment(
    int (*callback)(int), int value) {
  return callback(value);
}
