/* A static local scalar initializer list cannot contain an excess element. */
int main(void) {
  static int value = {1, 2};
  return value;
}
