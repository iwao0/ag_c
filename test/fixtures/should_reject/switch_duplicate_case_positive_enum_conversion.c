enum Positive {
  POSITIVE_ZERO,
  POSITIVE_ONE
};

int classify_positive(enum Positive value) {
  switch (value) {
    case -1:
      return 1;
    case 4294967295u:
      return 2;
  }
  return 0;
}
