int function_parameter_callback_return_array_element_const_qualifier(
    const int (*callback(void))[2]);

static const int (*read_const_callback_array(void))[2] {
  static const int values[2] = {20, 22};
  return &values;
}

int main(void) {
  return function_parameter_callback_return_array_element_const_qualifier(
      read_const_callback_array);
}
