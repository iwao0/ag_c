int select_value(unsigned char value) {
  switch (value) {
    case -1:
      return 1;
    case 4294967295u:
      return 2;
    default:
      return 0;
  }
}

int main(void) {
  return 0;
}
