// A case label cannot bypass a variably modified pointer typedef declaration.
int f(int n, int selector) {
  switch (selector) {
    typedef int (*row_pointer)[n];
    case 1:
      return sizeof(row_pointer);
    default:
      return 0;
  }
}

int main(void) {
  return 0;
}
