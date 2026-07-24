int main(void) {
  int *invalid = (int *)1.5;
  return invalid == 0;
}
