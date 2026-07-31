// A pointer to a VLA is variably modified, so its scope cannot be entered by goto.
int f(int n) {
  goto target;
  int (*pointer)[n];
target:
  return pointer != 0;
}

int main(void) {
  return 0;
}
