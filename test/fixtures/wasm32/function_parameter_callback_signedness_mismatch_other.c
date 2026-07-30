int function_parameter_callback_signedness(
    int (*callback)(unsigned int), int value) {
  return callback((unsigned int)value);
}
