/* An automatic scalar initializer cannot contain an extra brace level. */
int main(void) {
  int value = {{7}};
  return value;
}
