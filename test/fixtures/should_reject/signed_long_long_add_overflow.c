/* The typed ICE evaluator must also reject overflow at 64-bit width. */
enum {
  SIGNED_LONG_LONG_ADD_OVERFLOW =
      (int)(9223372036854775807LL + 1LL)
};
