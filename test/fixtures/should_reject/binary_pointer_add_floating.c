/* Pointer addition requires an integer offset, not a floating operand. */
int main(void) {
  int value = 1;
  return (&value + 1.0) != 0;
}
