/* An atomic type specifier cannot name a const-qualified type. */
_Atomic(const int) value;

int main(void) {
  return 0;
}
