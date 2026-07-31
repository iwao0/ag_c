/* An object pointer is not implicitly convertible to an integer parameter. */
static int read(int value) {
  return value;
}

int main(void) {
  int value = 1;
  return read(&value);
}
