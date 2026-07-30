static int function_return_pointer_const_qualifier_value = 42;

int *function_return_pointer_const_qualifier(void) {
  return &function_return_pointer_const_qualifier_value;
}
