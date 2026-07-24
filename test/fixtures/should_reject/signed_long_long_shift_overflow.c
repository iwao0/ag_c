/* 2LL << 63 is outside the corresponding unsigned 64-bit range. */
enum {
  SIGNED_LONG_LONG_SHIFT_OVERFLOW = (int)(2LL << 63)
};
