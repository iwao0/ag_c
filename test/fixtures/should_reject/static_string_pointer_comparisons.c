/* String-literal pointer comparisons are not integer constant expressions. */
#include <assert.h>

static int different_contents = "alpha" != "beta";
static int different_prefix = "a" != "ab";
static int ordered_same_literal = &"hello"[1] < &"hello"[4];
static int same_literal_address =
    &"world"[2] == "world" + 2;

int main(void) {
  assert(different_contents == 1);
  assert(different_prefix == 1);
  assert(ordered_same_literal == 1);
  assert(same_literal_address == 1);
  return 0;
}
