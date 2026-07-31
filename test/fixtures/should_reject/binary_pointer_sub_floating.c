/* Pointer subtraction by an offset requires an integer operand. */
int main(void) {
  int value = 1;
  return (&value - 1.0) != 0;
}
