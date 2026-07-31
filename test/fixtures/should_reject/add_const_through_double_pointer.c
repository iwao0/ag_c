/* A pointer-to-pointer conversion cannot add a qualifier to the inner pointee. */
int main(void) {
  int value = 7;
  int *mutable = &value;
  const int **invalid = &mutable;
  return **invalid;
}
