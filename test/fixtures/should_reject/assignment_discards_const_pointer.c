// A pointer assignment cannot discard the pointee's const qualification.
int main(void) {
  const int value = 5;
  const int *qualified = &value;
  int *pointer;
  pointer = qualified;
  return 0;
}
