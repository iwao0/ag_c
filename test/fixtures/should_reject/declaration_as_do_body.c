// A do body must be a statement, not a declaration.
int main(void) {
  do
    int value = 0;
  while (0);
  return 0;
}
