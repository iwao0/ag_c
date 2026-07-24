enum Positive {
  POSITIVE_ZERO,
  POSITIVE_ONE
};

_Static_assert(
    _Generic((enum Positive *)0,
             int *: 1,
             default: 0),
    "nonnegative enum is not compatible with int");

int main(void) { return 0; }
