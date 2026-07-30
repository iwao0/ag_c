int function_parameter_atomic_array_element(
    _Atomic(int) values[1]);

int main(void) {
  _Atomic(int) values[1] = {42};
  return function_parameter_atomic_array_element(values);
}
