int function_parameter_callback_return_array_bound(
    int (*callback(void))[2]);

static int (*read_two_element_callback_array(void))[2] {
  static int values[2] = {20, 22};
  return &values;
}

int main(void) {
  return function_parameter_callback_return_array_bound(
      read_two_element_callback_array);
}
