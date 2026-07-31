/* An atomic type specifier cannot name a const-qualified pointer type. */
int value;
_Atomic(int * const) pointer = &value;

int main(void) {
  return 0;
}
