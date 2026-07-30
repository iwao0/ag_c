int function_parameter_atomic_pointee(_Atomic(int) *value);

int main(void) {
  _Atomic(int) value = 42;
  return function_parameter_atomic_pointee(&value);
}
