int function_parameter_multidimensional_qualifier(
    const int values[static 1][2]);

int main(void) {
  const int values[1][2] = {{40, 42}};
  return function_parameter_multidimensional_qualifier(values);
}
