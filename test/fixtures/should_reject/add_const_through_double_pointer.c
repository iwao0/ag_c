int main(void) {
  int value = 7;
  int *mutable = &value;
  const int **invalid = &mutable;
  return **invalid;
}
