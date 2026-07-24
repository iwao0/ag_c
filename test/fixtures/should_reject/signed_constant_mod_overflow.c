/* INT_MIN % -1 has the same unrepresentable division and is not a valid ICE. */
enum {
  SIGNED_MOD_OVERFLOW = (-2147483647 - 1) % -1
};
