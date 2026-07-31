// A VLA typedef name has variably modified type and imposes the same jump constraint.
int f(int n) {
  goto target;
  typedef int row[n];
target:
  return sizeof(row);
}

int main(void) {
  return 0;
}
