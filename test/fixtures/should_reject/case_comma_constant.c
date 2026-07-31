// A case label cannot use a comma expression as its integer constant expression.
int main(void) {
  switch (1) {
    case (1, 1):
      return 0;
  }
  return 1;
}
