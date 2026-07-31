/* An atomic type specifier cannot name an atomic type. */
_Atomic(_Atomic(int)) value;

int main(void) {
  return 0;
}
