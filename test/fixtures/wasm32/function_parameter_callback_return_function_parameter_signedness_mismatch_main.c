int function_parameter_callback_return_function_parameter_signedness(
    int (*callback(void))(int), int value);

static int identity_factory_parameter(int value) {
  return value;
}

static int (*make_signed_parameter_callback(void))(int) {
  return identity_factory_parameter;
}

int main(void) {
  return function_parameter_callback_return_function_parameter_signedness(
      make_signed_parameter_callback, 42);
}
