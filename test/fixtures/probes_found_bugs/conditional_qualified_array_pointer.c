/* The conditional operator combines compatible pointers while retaining the
 * const qualification of the selected array element type. */
#include <assert.h>

static const int alternate_row[3] = {1, 2, 3};
static const int fixed_row[3] = {4, 5, 6};

static const int (*pick_row(int fixed))[3] {
  return fixed ? &fixed_row : &alternate_row;
}

int main(void) {
  assert((*pick_row(0))[1] == 2);
  assert((*pick_row(1))[1] == 5);
  assert((*pick_row(0))[2] == 3);
  return 0;
}
