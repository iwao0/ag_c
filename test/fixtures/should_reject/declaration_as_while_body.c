// A while body must be a statement, not a declaration.
int main(void) {
  while (0)
    int value = 0;
  return 0;
}
