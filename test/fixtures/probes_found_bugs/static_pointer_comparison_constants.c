/* Pointer comparisons in static initializers use resolved symbols and offsets. */
#include <assert.h>

static int values[3];
static struct {
  int first;
  int second;
} record;

static int function(void) {
  return 1;
}

static int same_address = &values[0] == &values[0];
static int different_address = &values[0] != &values[1];
static int ordered_address = &values[0] < &values[2];
static int member_order = &record.first < &record.second;
static int address_nonnull = &values[0] != 0;
static int null_equals_null = (int *)0 == (int *)0;
static int same_function = function == function;
static int selected_by_address =
    (&values[1] == values + 1) ? 7 : 9;
static int comparison_values[] = {
  &values[2] >= &values[1],
  &values[0] <= values,
  &values[2] > &values[0]
};

int main(void) {
  static int local_comparison =
      &values[1] != &values[2];

  assert(same_address == 1);
  assert(different_address == 1);
  assert(ordered_address == 1);
  assert(member_order == 1);
  assert(address_nonnull == 1);
  assert(null_equals_null == 1);
  assert(same_function == 1);
  assert(selected_by_address == 7);
  assert(comparison_values[0] == 1);
  assert(comparison_values[1] == 1);
  assert(comparison_values[2] == 1);
  assert(local_comparison == 1);
  return 0;
}
