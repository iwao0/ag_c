enum Negative {
  NEGATIVE_ONE = -1,
  NEGATIVE_ZERO
};

int classify_negative(enum Negative value) {
  switch (value) {
    case -1:
      return 1;
    case 4294967295u:
      return 2;
  }
  return 0;
}
