// A C11 case label must be followed by a statement, not a declaration.
int main(void) {
  switch (1) {
    case 1:
      int value = 0;
      return value;
  }
}
