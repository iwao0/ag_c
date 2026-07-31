int qualified_result(void);

const int qualified_result(void) {
  return 42;
}

int main(void) {
  return qualified_result() != 42;
}
