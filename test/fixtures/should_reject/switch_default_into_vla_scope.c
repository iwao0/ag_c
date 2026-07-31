// A default label cannot enter the scope of a VLA declared inside its switch.
int f(int n, int selector) {
  switch (selector) {
    int values[n];
    default:
      return sizeof(values);
  }
}

int main(void) {
  return 0;
}
