/*
 * A block-scope compound literal denotes one automatic object for its source
 * occurrence during an execution of the enclosing block.  Reaching the same
 * occurrence again reinitializes that object; distinct occurrences still
 * denote distinct simultaneously-live objects.
 */
#include <assert.h>

int main(void) {
  int iteration = 0;
  int *first = 0;
  int *current = 0;

repeat_literal:
  current = (int[2]){iteration, iteration + 10};
  if (iteration == 0) {
    first = current;
    current[0] = 99;
    iteration = 1;
    goto repeat_literal;
  }

  assert(current == first);
  assert(current[0] == 1);
  assert(current[1] == 11);

  {
    int *left = (int[1]){5};
    int *right = (int[1]){5};
    assert(left != right);
    assert(left[0] == right[0]);
  }
  return 0;
}
