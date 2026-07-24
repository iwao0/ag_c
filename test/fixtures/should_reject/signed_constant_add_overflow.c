/* A signed addition whose result is not representable is not a valid ICE. */
enum {
  SIGNED_ADD_OVERFLOW = 2147483647 + 1
};
