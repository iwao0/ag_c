// A for body must be a statement, not a declaration.
int main(void) {
  for (;;)
    int value = 0;
  return 0;
}
