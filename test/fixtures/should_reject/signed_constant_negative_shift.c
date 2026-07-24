/* Left-shifting a negative signed value is not a valid ICE. */
enum {
  SIGNED_NEGATIVE_SHIFT = -1 << 1
};
