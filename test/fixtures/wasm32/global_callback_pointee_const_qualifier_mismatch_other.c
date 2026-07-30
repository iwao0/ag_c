static int global_callback_pointee_const_qualifier_impl(
    int *value) {
  return *value;
}

int (*global_callback_pointee_const_qualifier)(int *) =
    global_callback_pointee_const_qualifier_impl;
