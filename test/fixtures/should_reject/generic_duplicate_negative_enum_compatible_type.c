enum Negative {
  NEGATIVE_ONE = -1,
  NEGATIVE_ZERO
};

int classify_negative(enum Negative value) {
  return _Generic(
      value,
      enum Negative: 1,
      int: 2,
      default: 0);
}
