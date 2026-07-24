int main(void) {
  int left = 1;
  int right = 2;
  int *pointer = &(left, right);
  return pointer != 0;
}
