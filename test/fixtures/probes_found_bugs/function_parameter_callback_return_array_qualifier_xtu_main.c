int invoke_callback_return_array_qualifier(
    const volatile int (*callback(void))[3]);

static const volatile int (*read_callback_return_array(void))[3] {
  static const volatile int values[3] = {10, 20, 12};
  return &values;
}

int main(void) {
  return invoke_callback_return_array_qualifier(
      read_callback_return_array);
}
