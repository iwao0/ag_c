int select_value(int value) {
  switch (value) {
    case -2:
      return 1;
    case 4294967294u:
      return 2;
    default:
      return 0;
  }
}

int main(void) {
  return 0;
}
