/*
 * The comma operator evaluates its left operand once and applies the normal
 * value conversions to its right operand. Arrays, VLAs, strings, and function
 * designators therefore decay, while aggregate values remain copyable.
 */
#include <assert.h>

typedef int (*Unary)(int);

struct Pair {
  int first;
  long second;
};

static int effects;

static void note(void) {
  effects++;
}

static int add_one(int value) {
  return value + 1;
}

int main(void) {
  int values[4] = {3, 5, 7, 9};
  int matrix[2][3] = {{11, 13, 15}, {17, 19, 21}};
  int extent = 5;
  int variable[extent];
  const int constant = 23;
  volatile int observable = 29;
  struct Pair pair = {31, 37};

  for (int i = 0; i < extent; i++)
    variable[i] = 41 + i;

  assert(_Generic(
             (note(), values),
             int *: 1,
             default: 0) == 1);
  assert(_Generic(
             (note(), add_one),
             Unary: 1,
             default: 0) == 1);
  assert(effects == 0);

  assert((note(), add_one)(41) == 42);

  int *element_pointer = (note(), values);
  assert(element_pointer == values);
  assert((note(), values)[2] == 7);

  int (*row_pointer)[3] = (note(), matrix);
  assert(row_pointer == matrix);
  assert((note(), matrix)[1][2] == 21);

  char *text = (note(), "comma");
  assert(text[0] == 'c' && text[4] == 'a' && text[5] == '\0');

  int *variable_pointer = (note(), variable);
  assert(variable_pointer == variable);
  assert((note(), variable)[extent - 1] == 45);

  int constant_value = (note(), constant);
  int observable_value = (note(), observable);
  assert(constant_value == 23);
  assert(observable_value == 29);

  struct Pair copied = (note(), pair);
  assert(copied.first == 31);
  assert(copied.second == 37);

  assert(effects == 11);
  return 0;
}
