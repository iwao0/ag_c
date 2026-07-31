int *pointer_result(void);

int *const pointer_result(void) {
  static int value = 42;
  return &value;
}

int main(void) {
  return *pointer_result() != 42;
}
