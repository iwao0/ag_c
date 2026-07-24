enum Positive {
  POSITIVE_ZERO,
  POSITIVE_ONE
};

int classify_positive(enum Positive value) {
  return _Generic(
      value,
      enum Positive: 1,
      unsigned int: 2,
      default: 0);
}
