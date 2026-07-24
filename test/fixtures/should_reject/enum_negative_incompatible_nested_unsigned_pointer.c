enum Negative {
  NEGATIVE_ONE = -1,
  NEGATIVE_ZERO
};

_Static_assert(
    _Generic((enum Negative **)0,
             unsigned int **: 1,
             default: 0),
    "nested negative enum pointer is not compatible with nested unsigned pointer");
