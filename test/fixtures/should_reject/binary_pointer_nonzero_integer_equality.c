/* Pointer equality permits a null pointer constant, not a nonzero integer. */
int main(void) {
  int value = 1;
  return &value == 1;
}
