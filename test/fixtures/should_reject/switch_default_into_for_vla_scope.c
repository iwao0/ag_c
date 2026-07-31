// A default label cannot enter a loop by bypassing its VLA initializer.
int f(int n, int selector) {
  switch (selector) {
    for (int values[n];;) {
      default:
        return sizeof(values);
    }
  }
}

int main(void) {
  return 0;
}
