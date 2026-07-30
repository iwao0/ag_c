int function_parameter_callback_signedness(
    int callback(int), int value);

static int identity_signed_value(int value) {
  return value;
}

int main(void) {
  return function_parameter_callback_signedness(
      identity_signed_value, 42);
}
