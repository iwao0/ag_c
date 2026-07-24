enum Positive {
  POSITIVE_ZERO,
  POSITIVE_ONE
};

_Static_assert(
    _Generic((enum Positive **)0,
             int **: 1,
             default: 0),
    "nested positive enum pointer is not compatible with nested int pointer");
