/* A block-scope static pointer cannot be initialized with an automatic object's address. */
int main(void) {
  int automatic_value = 1;
  static int *pointer = &automatic_value;
  return pointer != 0;
}
