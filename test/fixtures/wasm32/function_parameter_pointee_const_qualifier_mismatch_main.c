int function_parameter_pointee_const_qualifier(
    const int *value);

int main(void) {
  int value = 42;
  return function_parameter_pointee_const_qualifier(&value);
}
