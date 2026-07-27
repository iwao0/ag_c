// A generic selection inherits the selected expression's type and value
// category. Arrays, strings, VLAs, functions, and qualified lvalues must keep
// those properties until the surrounding operator applies its own decay.
typedef int row3[3];
typedef int unary_function(int);

static int add_one(int value) {
  return value + 1;
}

static int add_two(int value) {
  return value + 2;
}

int main(void) {
  row3 values = {1, 2, 3};
  row3 other = {4, 5, 6};
  const int constant = 7;
  int mutable = 8;
  int length = 5;
  int variable_values[length];

  if (sizeof(_Generic(0, int: values, default: other)) !=
      sizeof(row3))
    return 10;
  _Static_assert(
      sizeof(_Generic(0, int: "abc", default: "x")) == 4,
      "selected string remains an array under sizeof");
  if (sizeof(_Generic(0, int: variable_values, default: values)) !=
      5 * sizeof(int))
    return 11;

  int (*array_pointer)[3] =
      &_Generic(0, int: values, default: other);
  int *element_pointer =
      _Generic(0, int: values, default: other);
  unary_function *function_pointer =
      &_Generic(0, int: add_one, default: add_two);
  const int *constant_pointer =
      &_Generic(0, int: constant, default: mutable);

  (*array_pointer)[1] = 9;
  return element_pointer == values &&
                 function_pointer(4) == 5 &&
                 *constant_pointer == 7 &&
                 values[1] == 9
             ? 0
             : 1;
}
