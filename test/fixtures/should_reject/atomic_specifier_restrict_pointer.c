/* The atomic type specifier also makes restrict invalid for a pointer. */
restrict _Atomic(int *) invalid_pointer;

int main(void) {
  return 0;
}
