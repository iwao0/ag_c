// A goto cannot bypass a VLA declaration and enter its scope.
int f(int n) {
  goto target;
  int values[n];
target:
  return sizeof(values);
}

int main(void) {
  return 0;
}
