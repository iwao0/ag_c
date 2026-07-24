/* A signed subtraction whose result is not representable is not a valid ICE. */
enum {
  SIGNED_SUB_OVERFLOW = -2147483647 - 2
};
