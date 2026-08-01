/* A block-scope static scalar still requires a constant initializer. */
int main(void) {
  static int source = 1;
  static int copy = source;
  return copy;
}
