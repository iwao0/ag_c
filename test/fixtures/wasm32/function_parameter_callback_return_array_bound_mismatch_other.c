int function_parameter_callback_return_array_bound(
    int (*(*callback)(void))[3]) {
  return (*callback())[0];
}
