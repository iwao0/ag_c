/* Static address constant expressions must evaluate only the selected branch. */
#include <assert.h>
#include <stddef.h>

static int values[5] = {10, 20, 30, 40, 50};
static struct {
  char prefix;
  int scalar;
  int nested[2];
} record = {'x', 60, {70, 80}};

static int return_one(void) { return 1; }
static int return_two(void) { return 2; }
static int *return_values(void) { return values; }
static int side_effect;

static int *array_decay = values;
static int *array_address = &values[2];
static int *array_add = values + 3;
static int *reversed_add = 1 + values;
static char *string_add = "hello" + 2;
static int *conditional_true = 1 ? &values[1] : &values[4];
static int *conditional_false = 0 ? &values[1] : &values[4];
static int *floating_condition = -0.25 ? &values[3] : &values[0];
static int *nested_condition =
    0 ? &values[0] : ((1u << 31) ? &values[4] : 0);
static int *conditional_null = 1 ? &values[2] : 0;
static int *selected_address_with_invalid_unselected =
    1 ? &values[0] : ((void)(1 / 0), &values[4]);
static int *selected_without_call =
    1 ? &values[2] : return_values();
static int *selected_without_assignment =
    1 ? &values[1] : (side_effect = 1, &values[0]);
static int *selected_null = 0 ? &values[0] : 0;
static int *casted_address = (int *)(void *)&values[3];
static int *member_address = &record.nested[1];
static ptrdiff_t array_difference = &values[4] - &values[1];
static ptrdiff_t member_difference =
    &record.nested[1] - &record.nested[0];
static int (*function_choice)(void) =
    0 ? return_one : return_two;
static struct {
  int *element;
  char *text;
  int (*function)(void);
} address_record = {
  1 ? &values[4] : &values[0],
  0 ? "bad" : "ok",
  1 ? return_one : return_two
};

int main(void) {
  static int *local_choice =
      0.0 ? &values[0] : &record.nested[0];
  assert(*array_decay == 10);
  assert(*array_address == 30);
  assert(*array_add == 40);
  assert(*reversed_add == 20);
  assert(*string_add == 'l');
  assert(*conditional_true == 20);
  assert(*conditional_false == 50);
  assert(*floating_condition == 40);
  assert(*nested_condition == 50);
  assert(*conditional_null == 30);
  assert(*selected_address_with_invalid_unselected == 10);
  assert(*selected_without_call == 30);
  assert(*selected_without_assignment == 20);
  assert(side_effect == 0);
  assert(selected_null == 0);
  assert(*casted_address == 40);
  assert(*member_address == 80);
  assert(array_difference == 3);
  assert(member_difference == 1);
  assert(function_choice() == 2);
  assert(*address_record.element == 50);
  assert(address_record.text[0] == 'o');
  assert(address_record.function() == 1);
  assert(*local_choice == 70);
  return 0;
}
