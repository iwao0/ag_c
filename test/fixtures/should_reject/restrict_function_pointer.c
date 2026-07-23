// `restrict` may qualify only a pointer to an object or incomplete type
// (C11 6.7.3p2), not a pointer to a function type.
static int answer(void) {
  return 42;
}

int main(void) {
  int (*restrict callback)(void) = answer;
  return callback() != 42;
}
