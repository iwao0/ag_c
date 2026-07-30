typedef int atomic_result_callback_t(int);

static int identity(int value) {
  return value;
}

atomic_result_callback_t *
function_return_atomic_function_pointer(void) {
  return identity;
}
