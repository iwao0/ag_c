// A case label cannot enter a loop by bypassing its variably modified initializer.
int f(int n, int selector) {
  switch (selector) {
    for (int values[n];;) {
      case 1:
        return sizeof(values);
    }
    default:
      return 0;
  }
}

int main(void) {
  return 0;
}
