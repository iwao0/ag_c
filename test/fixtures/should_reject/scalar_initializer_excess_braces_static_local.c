/* A static local scalar initializer cannot contain an extra brace level. */
int main(void) {
  static int value = {{7}};
  return value;
}
