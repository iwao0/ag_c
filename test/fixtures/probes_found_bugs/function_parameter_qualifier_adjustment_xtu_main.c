int function_parameter_qualifier_adjustment(
    const int value,
    int fixed_pointer[static const restrict 1],
    int variable_pointer[const restrict *]);

int main(void) {
  int fixed_value = 2;
  int variable_value = 0;
  return function_parameter_qualifier_adjustment(
      40, &fixed_value, &variable_value);
}
