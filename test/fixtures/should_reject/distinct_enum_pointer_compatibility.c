enum First {
  FIRST_ZERO,
  FIRST_ONE
};

enum Second {
  SECOND_ZERO,
  SECOND_ONE
};

_Static_assert(
    _Generic((enum First *)0,
             enum Second *: 1,
             default: 0),
    "distinct enum types remain incompatible");
