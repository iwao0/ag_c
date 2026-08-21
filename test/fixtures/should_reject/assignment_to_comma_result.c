/* A comma expression is not an lvalue in C11. */
int main(void) {
  int left = 1;
  int right = 2;
  (left, right) = 3;
  return right;
}
