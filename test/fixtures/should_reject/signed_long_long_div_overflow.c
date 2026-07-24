/* LLONG_MIN / -1 is not representable even when cast after the operation. */
enum {
  SIGNED_LONG_LONG_DIV_OVERFLOW =
      (int)((-9223372036854775807LL - 1LL) / -1LL)
};
