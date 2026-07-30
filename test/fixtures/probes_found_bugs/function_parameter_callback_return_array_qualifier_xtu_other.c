int invoke_callback_return_array_qualifier(
    const volatile int (*(*callback)(void))[3]) {
  const volatile int (*values)[3] = callback();
  return (*values)[0] + (*values)[1] + (*values)[2];
}
