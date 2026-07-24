int main(void) {
  const int value = 7;
  const int *qualified = &value;
  int **invalid = &qualified;
  return **invalid;
}
