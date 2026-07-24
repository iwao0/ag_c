static int answer(void) {
  return 42;
}

int main(void) {
  int choose = 1;
  void *invalid = choose ? answer : (void *)0;
  return invalid == 0;
}
