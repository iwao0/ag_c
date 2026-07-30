int function_parameter_callback_return_array_element_const_qualifier(
    int (*(*callback)(void))[2]) {
  return (*callback())[0];
}
