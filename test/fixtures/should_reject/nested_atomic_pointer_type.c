/* An atomic type specifier cannot name an atomic-qualified pointer type. */
int value;
_Atomic(int * _Atomic) pointer = &value;

int main(void) {
  return 0;
}
