/* INT_MIN / -1 is not representable as int and is not a valid ICE. */
enum {
  SIGNED_DIV_OVERFLOW = (-2147483647 - 1) / -1
};
