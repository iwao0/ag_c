#include <assert.h>

struct Pair {
  int left;
  int right;
};

typedef unsigned short small_t;

int narrow_char(value)
char value;
{
  assert(sizeof value == 1);
  return value;
}

int narrow_unsigned(value)
unsigned char value;
{
  assert(sizeof value == 1);
  return value;
}

int narrow_short(value)
small_t value;
{
  assert(sizeof value == 2);
  return value;
}

double narrow_float(value)
float value;
{
  assert(sizeof value == 4);
  return value + 0.25f;
}

int sum_array(values, count)
int count;
register int values[];
{
  int sum = 0;
  int index;
  for (index = 0; index < count; index++)
    sum += values[index];
  return sum;
}

int apply(callback, value)
int callback(int);
int value;
{
  return callback(value);
}

int add_one(int value) {
  return value + 1;
}

int pair_sum(pair)
struct Pair pair;
{
  return pair.left + pair.right;
}

int sum_pair(left, right)
int left, right;
{
  return left + right;
}

int compatible_integer(int, int);
int compatible_integer(left, right)
char left;
short right;
{
  return left + right;
}

double compatible_float(double);
double compatible_float(value)
float value;
{
  return value;
}

int main(void) {
  int values[] = {3, 5, 7};
  struct Pair pair = {11, 13};
  assert(narrow_char(257) == 1);
  assert(narrow_unsigned(513) == 1);
  assert(narrow_short(65537) == 1);
  assert(narrow_float(1.5f) == 1.75);
  assert(sum_array(values, 3) == 15);
  assert(apply(add_one, 41) == 42);
  assert(pair_sum(pair) == 24);
  assert(sum_pair(17, 19) == 36);
  assert(compatible_integer(257, 65538) == 3);
  assert(compatible_float(2.5f) == 2.5);
  return 0;
}
