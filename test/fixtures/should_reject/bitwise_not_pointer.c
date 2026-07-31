/* Bitwise complement requires an integer operand. */
int main(void) {
  int value = 1;
  int *pointer = &value;
  return ~pointer != 0;
}
