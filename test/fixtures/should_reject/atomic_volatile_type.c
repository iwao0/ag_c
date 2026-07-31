/* An atomic type specifier cannot name a volatile-qualified type. */
_Atomic(volatile int) value;

int main(void) {
  return 0;
}
