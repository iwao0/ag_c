// Both case values become ULONG_MAX in the 64-bit unsigned controlling type.
int select_value(unsigned long value) {
  switch (value) {
    case -1:
      return 1;
    case 18446744073709551615UL:
      return 2;
    default:
      return 0;
  }
}

int main(void) {
  return 0;
}
