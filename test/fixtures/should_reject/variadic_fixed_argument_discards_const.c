/* A fixed parameter of a variadic function cannot discard pointee const. */
static int read(int *value, ...) {
  return *value;
}

int main(void) {
  const int value = 1;
  return read(&value, 2);
}
