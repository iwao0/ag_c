static int answer(void) {
  return 42;
}

int main(void) {
  void *invalid = answer;
  return invalid == 0;
}
