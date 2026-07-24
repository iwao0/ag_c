/* Negating the minimum signed value is not a valid ICE. */
enum {
  SIGNED_NEGATE_OVERFLOW = -(-2147483647 - 1)
};
