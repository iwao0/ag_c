int function_parameter_atomic_type(_Atomic(int) value);

int main(void) {
  _Atomic(int) value = 42;
  return function_parameter_atomic_type(value);
}
