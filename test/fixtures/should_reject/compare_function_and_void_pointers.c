static int answer(void) {
  return 42;
}

int main(void) {
  void *object_pointer = 0;
  return answer == object_pointer;
}
