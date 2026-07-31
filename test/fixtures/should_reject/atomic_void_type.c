/* An atomic type specifier cannot name void. */
_Atomic(void) value;

int main(void) {
  return 0;
}
