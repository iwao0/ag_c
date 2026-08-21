#include <assert.h>

typedef signed int SignedValue;
typedef int SignedValue;
typedef int FixedValues[3];
typedef int FixedValues[3];
typedef int UnaryFunction(int);
typedef int UnaryFunction(int);

static int increment(int value) { return value + 1; }

static int exercise_vla_shadow(int count) {
  typedef int RuntimeValues[count];
  RuntimeValues outer;
  outer[count - 1] = 7;

  {
    typedef int RuntimeValues[count + 1];
    RuntimeValues inner;
    inner[count] = 11;
    assert(sizeof(inner) / sizeof(inner[0]) == (unsigned long)(count + 1));
    outer[count - 1] += inner[count];
  }

  assert(sizeof(outer) / sizeof(outer[0]) == (unsigned long)count);
  return outer[count - 1];
}

int main(void) {
  typedef unsigned long Word;
  typedef unsigned long Word;
  FixedValues fixed = {1, 2, 3};
  UnaryFunction *function = increment;
  Word total = (Word)(fixed[0] + fixed[1] + fixed[2]);

  assert(sizeof(SignedValue) == sizeof(int));
  assert(total == 6);
  assert(function(4) == 5);
  assert(exercise_vla_shadow(2) == 18);
  return 0;
}
