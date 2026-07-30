int function_parameter_callback_return_function_result_signedness(
    int (*callback(void))(int), int value);

static int identity_factory_result(int value) {
  return value;
}

static int (*make_signed_result_callback(void))(int) {
  return identity_factory_result;
}

int main(void) {
  return function_parameter_callback_return_function_result_signedness(
      make_signed_result_callback, 42);
}
