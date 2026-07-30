int function_parameter_callback_return_pointee_const_qualifier(
    const int *callback(void));

static const int *read_const_callback_return(void) {
  static const int value = 42;
  return &value;
}

int main(void) {
  return function_parameter_callback_return_pointee_const_qualifier(
      read_const_callback_return);
}
