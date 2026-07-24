int f(int n, int selector) {
  switch (selector) {
    typedef int row[n];
    case 1:
      return sizeof(row);
    default:
      return 0;
  }
}

int main(void) {
  return 0;
}
