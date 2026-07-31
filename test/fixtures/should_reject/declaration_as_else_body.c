// An else body must be a statement, not a declaration.
int main(void) {
  if (1)
    return 0;
  else
    int value = 0;
  return 1;
}
