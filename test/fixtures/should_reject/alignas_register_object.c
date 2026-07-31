/* An alignment specifier cannot be applied to a register object. */
int main(void) {
  register _Alignas(8) int value = 0;
  return value;
}
