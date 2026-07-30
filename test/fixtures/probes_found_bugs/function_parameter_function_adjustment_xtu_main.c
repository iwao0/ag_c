int function_parameter_function_adjustment(
    int callback(int), int value);

static int add_two(int value) {
  return value + 2;
}

int main(void) {
  return function_parameter_function_adjustment(add_two, 40);
}
