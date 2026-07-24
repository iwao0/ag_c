/* A bit-field width cannot be formed from signed overflow. */
struct SignedOverflowWidth {
  int value : 2147483647 + 1;
};
