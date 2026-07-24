/* Cross-symbol one-past equality depends on layout and is not constant. */
#include <assert.h>

static int first_array[2];
static int second_array[2];

static int one_past_comparison =
    &first_array[2] == &second_array[0];

int main(void) {
  assert(one_past_comparison ==
         (&first_array[2] == &second_array[0]));
  return 0;
}
