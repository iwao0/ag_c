// A goto cannot enter a loop body by bypassing a VLA in the for initializer.
int f(int n) {
  goto target;
  for (int values[n];;) {
target:
    return sizeof(values);
  }
}

int main(void) {
  return 0;
}
