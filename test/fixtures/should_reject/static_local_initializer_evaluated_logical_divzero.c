// A block-scope static initializer uses the same constant-expression constraints.
int main(void) {
  static int value = 1 && (1 / 0);
  return value;
}
