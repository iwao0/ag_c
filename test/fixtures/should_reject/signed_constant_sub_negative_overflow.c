/* Subtracting a negative value must not overflow above INT_MAX. */
enum {
  SIGNED_SUB_NEGATIVE_OVERFLOW = 2147483647 - -1
};
