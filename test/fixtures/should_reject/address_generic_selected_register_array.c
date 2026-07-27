/* A selected register array is still not addressable. */
int main(void) {
  register int values[3] = {1, 2, 3};
  return &_Generic(0, int: values, default: values) != 0;
}
