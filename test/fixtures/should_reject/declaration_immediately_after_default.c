// A C11 default label must be followed by a statement, not a declaration.
int main(void) {
  switch (1) {
    default:
      int value = 0;
      return value;
  }
}
