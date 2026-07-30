static int global_callback_parameter_qualifier_impl(
    int value, int *pointer, int *variable_length_pointer) {
  return value + *pointer + *variable_length_pointer;
}

int (*global_callback_parameter_qualifier)(
    int, int *, int *) =
    global_callback_parameter_qualifier_impl;
