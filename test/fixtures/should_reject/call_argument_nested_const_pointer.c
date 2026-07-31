/* Qualifying the intermediate pointer does not make int ** compatible with const int **. */
static int read(const int *const *value) {
  return **value;
}

int main(void) {
  int value = 1;
  int *pointer = &value;
  return read(&pointer);
}
