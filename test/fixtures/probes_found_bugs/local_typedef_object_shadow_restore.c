/* An ordinary identifier may hide a typedef in a nested block. The typedef
 * must become visible again after the block; tags, members, and labels remain
 * separate C namespaces throughout. */
#include <assert.h>

typedef int Number;

static int evaluate(void) {
  Number outer = 3;
  int total = outer;

  {
    int Number = 4;
    struct Number {
      int Number;
    } value = {5};

    total += Number;
    total += value.Number;
  }

  Number restored = 7;
  total += restored;
  goto Number;

Number:
  return total;
}

int main(void) {
  assert(evaluate() == 19);
  return 0;
}
