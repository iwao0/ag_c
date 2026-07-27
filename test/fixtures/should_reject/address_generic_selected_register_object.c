/* A selected register object is still not addressable. */
int main(void) {
  register int value = 1;
  return &_Generic(0, int: value, default: value) != 0;
}
