/* A nonzero integer is not implicitly convertible to an object pointer. */
static int read(int *value) {
  return value != 0;
}

int main(void) {
  return read(1);
}
