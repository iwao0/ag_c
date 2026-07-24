/* An overflowing signed expression is not an integer constant expression. */
_Static_assert(
    (2147483647 + 1) == (-2147483647 - 1),
    "signed overflow must not be folded");
