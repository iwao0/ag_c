/* A signed left shift whose result is not representable is not a valid ICE. */
enum {
  SIGNED_SHIFT_OVERFLOW = 2 << 31
};
