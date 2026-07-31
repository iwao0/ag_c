/* Pointer subtraction requires compatible pointed-to object types. */
int main(void) {
  int left = 1;
  _Atomic int right = 1;
  return (int)(&left - &right);
}
