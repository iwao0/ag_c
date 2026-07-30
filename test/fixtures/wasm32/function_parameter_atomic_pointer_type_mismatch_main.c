int function_parameter_atomic_pointer(_Atomic(int *) value);

int main(void) {
  int value = 42;
  _Atomic(int *) pointer = &value;
  return function_parameter_atomic_pointer(pointer);
}
