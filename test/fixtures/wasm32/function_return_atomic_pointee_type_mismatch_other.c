static int value = 42;

int *function_return_atomic_pointee(void) {
  return &value;
}
