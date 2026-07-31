/* A nested pointer conversion cannot add const only to the deepest pointee. */
static int read(const int **value) {
  return **value;
}

int main(void) {
  int value = 1;
  int *pointer = &value;
  return read(&pointer);
}
