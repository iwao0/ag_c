int main(void) {
  int value = 7;
  int *pointer = &value;
  void **invalid = &pointer;
  return invalid == 0;
}
