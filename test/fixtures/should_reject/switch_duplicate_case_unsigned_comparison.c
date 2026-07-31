// The unsigned comparison is false, so its case value duplicates zero.
int select_value(int value) {
  switch (value) {
    case 0:
      return 1;
    case 0xffffffffu > -1:
      return 2;
    default:
      return 0;
  }
}

int main(void) {
  return 0;
}
