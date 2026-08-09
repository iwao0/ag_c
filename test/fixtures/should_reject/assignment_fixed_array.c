/* An array object is not a modifiable lvalue for simple assignment. */
int main(void) {
  int source[2] = {1, 2};
  int values[2] = {0, 0};
  values = source;
  return values[0];
}
