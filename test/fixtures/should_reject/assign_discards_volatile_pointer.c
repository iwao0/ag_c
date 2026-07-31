/* An implicit pointer assignment cannot discard a pointee volatile qualifier. */
int main(void) {
  volatile int value = 7;
  volatile int *qualified = &value;
  int *invalid = qualified;
  return *invalid;
}
