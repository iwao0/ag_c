/* A function pointer cannot implicitly convert to void *. */
static int answer(void) {
  return 42;
}

int main(void) {
  void *invalid = answer;
  return invalid == 0;
}
