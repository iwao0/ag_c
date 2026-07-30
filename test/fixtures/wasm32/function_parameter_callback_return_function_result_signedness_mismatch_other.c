int function_parameter_callback_return_function_result_signedness(
    unsigned int (*(*callback)(void))(int), int value) {
  return (int)callback()(value);
}
