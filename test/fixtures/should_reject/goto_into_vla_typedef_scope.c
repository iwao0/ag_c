int f(int n) {
  goto target;
  typedef int row[n];
target:
  return sizeof(row);
}

int main(void) {
  return 0;
}
