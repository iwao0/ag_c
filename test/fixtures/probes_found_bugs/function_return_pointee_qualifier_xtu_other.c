static const volatile int function_return_pointee_qualifier_value = 42;

const volatile int *function_return_pointee_qualifier(void) {
  return &function_return_pointee_qualifier_value;
}
