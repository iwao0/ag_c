int invoke_callback_return_qualifier(
    const volatile int *callback(void));

static const volatile int *read_callback_return_qualifier(void) {
  static const volatile int value = 42;
  return &value;
}

int main(void) {
  return invoke_callback_return_qualifier(
      read_callback_return_qualifier);
}
