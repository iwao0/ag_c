static const int qualified_result(void) {
  return 42;
}

int main(void) {
  int (*invalid)(void) = qualified_result;
  return invalid() != 42;
}
