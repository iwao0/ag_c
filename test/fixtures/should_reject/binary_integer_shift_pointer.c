/* Shift operators require integer operands on both sides. */
int main(void) {
  int value = 1;
  return 1 << &value;
}
