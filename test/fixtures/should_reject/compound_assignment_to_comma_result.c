/* A comma expression is not a modifiable lvalue for +=. */
int main(void) {
  int left = 1;
  int right = 2;
  (left, right) += 3;
  return right;
}
