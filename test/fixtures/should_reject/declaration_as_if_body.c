// An if body must be a statement, not a declaration.
int main(void) {
  if (1)
    int value = 0;
  return 0;
}
