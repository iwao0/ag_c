int function_parameter_callback_return_pointee_const_qualifier(
    int *(*callback)(void)) {
  return *callback();
}
