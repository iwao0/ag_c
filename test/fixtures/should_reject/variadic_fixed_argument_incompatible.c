/* A fixed parameter of a variadic function still requires a compatible argument. */
static int read(int *value, ...) {
  return *value;
}

int main(void) {
  double value = 1.0;
  return read(&value, 2);
}
