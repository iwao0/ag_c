/*
 * sizeof a variably modified type evaluates its bound only when execution
 * reaches that sizeof expression.  Conditional and logical operators must
 * therefore suppress bounds in unselected or short-circuited operands.
 */
#include <assert.h>
#include <stddef.h>

static size_t conditional_size(int choose_left, int *left_effect,
                               int *right_effect) {
  int left_length = 3;
  int right_length = 5;

  return choose_left
             ? sizeof(int[((*left_effect)++, left_length)])
             : sizeof(int[((*right_effect)++, right_length)]);
}

int main(void) {
  int left_effect = 0;
  int right_effect = 0;
  int logical_effect = 0;

  assert(conditional_size(1, &left_effect, &right_effect) ==
         3 * sizeof(int));
  assert(left_effect == 1);
  assert(right_effect == 0);

  assert(conditional_size(0, &left_effect, &right_effect) ==
         5 * sizeof(int));
  assert(left_effect == 1);
  assert(right_effect == 1);

  assert((0 && sizeof(int[(logical_effect++, 2)])) == 0);
  assert(logical_effect == 0);
  assert((1 || sizeof(int[(logical_effect++, 2)])) == 1);
  assert(logical_effect == 0);

  assert((1 && sizeof(int[(logical_effect++, 2)])) == 1);
  assert(logical_effect == 1);
  assert((0 || sizeof(int[(logical_effect++, 2)])) == 1);
  assert(logical_effect == 2);
  return 0;
}
