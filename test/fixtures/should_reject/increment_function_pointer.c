static int answer(void) {
  return 42;
}

int main(void) {
  int (*pointer)(void) = answer;
  ++pointer;
  return 0;
}
