/* A conditional expression cannot combine function and object pointers. */
static int answer(void) {
  return 42;
}

int main(void) {
  void *object_pointer = 0;
  return (1 ? answer : object_pointer) != 0;
}
