int select_value(unsigned value) {
  switch (value) {
    case 0x7fffffffu:
      return 1;
    case ~0u >> 1:
      return 2;
    default:
      return 0;
  }
}

int main(void) {
  return 0;
}
