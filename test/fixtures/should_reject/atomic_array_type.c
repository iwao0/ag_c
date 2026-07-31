/* An atomic type specifier cannot name an array type. */
_Atomic(int[2]) value;

int main(void) {
  return 0;
}
