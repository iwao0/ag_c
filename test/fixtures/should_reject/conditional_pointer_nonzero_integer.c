/* A conditional pointer operand can pair with zero, not a nonzero integer. */
int main(void) {
  int value = 1;
  return *(1 ? &value : 1);
}
