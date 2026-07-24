/* Signed addition must reject the lower-bound overflow direction too. */
enum {
  SIGNED_ADD_UNDERFLOW = (-2147483647 - 1) + -1
};
