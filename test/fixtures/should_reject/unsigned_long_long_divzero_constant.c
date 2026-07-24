/* Division by zero cannot form an integer constant expression. */
_Static_assert(
    (18446744073709551615ULL / 0ULL) == 0ULL,
    "division by zero is not a constant");
