/* A register object is not addressable. */
int main(void) {
  register int value = 1;
  return &value != 0;
}
