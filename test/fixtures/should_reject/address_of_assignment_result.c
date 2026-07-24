int main(void) {
  int value = 1;
  int *pointer = &(value = 2);
  return pointer != 0;
}
