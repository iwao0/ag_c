// Unsigned constant-expression wrapping makes the second case equal to 1.
int select_value(unsigned value) {
  switch (value) {
    case 1:
      return 1;
    case 4294967295u + 2u:
      return 2;
    default:
      return 0;
  }
}

int main(void) {
  return 0;
}
