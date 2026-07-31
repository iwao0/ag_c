/* Unary plus requires an arithmetic operand. */
int main(void) {
  int value;
  int *pointer = &value;
  return +pointer == 0;
}
