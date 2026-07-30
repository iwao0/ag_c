int function_parameter_callback_return_function_parameter_signedness(
    int (*(*callback)(void))(unsigned int), int value) {
  return callback()((unsigned int)value);
}
