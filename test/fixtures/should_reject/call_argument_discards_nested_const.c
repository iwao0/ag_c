/* A nested pointer conversion cannot discard a pointee const qualifier. */
static int read(int **value) {
  return **value;
}

int main(void) {
  const int value = 1;
  const int *pointer = &value;
  return read(&pointer);
}
