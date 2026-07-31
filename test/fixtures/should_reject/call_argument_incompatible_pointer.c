/* A function argument cannot convert between incompatible object pointers. */
static int read(int *value) {
  return *value;
}

int main(void) {
  double value = 1.0;
  return read(&value);
}
