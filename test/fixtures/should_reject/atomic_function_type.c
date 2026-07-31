/* An atomic type specifier cannot name a function type. */
_Atomic(int(void)) value;

int main(void) {
  return 0;
}
