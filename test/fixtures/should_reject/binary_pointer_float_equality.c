/* A pointer and a floating value are not valid equality operands. */
int main(void) {
  int value = 1;
  return &value == 1.0;
}
