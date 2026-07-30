int invoke_callback_return_qualifier(
    const volatile int *(*callback)(void)) {
  return *callback();
}
