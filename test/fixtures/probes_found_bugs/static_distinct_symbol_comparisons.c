/* Distinct object interiors and functions have unequal addresses. */
#include <assert.h>

static int first_scalar;
static int second_scalar;
static int first_array[2];
static int second_array[2];

static int first_function(void) { return 1; }
static int second_function(void) { return 2; }

static int distinct_scalars = &first_scalar != &second_scalar;
static int distinct_array_starts =
    &first_array[0] != &second_array[0];
static int distinct_array_members =
    &first_array[1] != &second_array[1];
static int distinct_functions =
    first_function != second_function;

int main(void) {
  assert(distinct_scalars == (&first_scalar != &second_scalar));
  assert(distinct_array_starts ==
         (&first_array[0] != &second_array[0]));
  assert(distinct_array_members ==
         (&first_array[1] != &second_array[1]));
  assert(distinct_functions ==
         (first_function != second_function));
  return 0;
}
