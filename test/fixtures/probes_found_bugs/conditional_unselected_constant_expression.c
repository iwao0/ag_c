#include <assert.h>

static int runtime_value(void) {
  return 99;
}

static void set_value(int *target, int value) {
  *target = value;
}

enum conditional_constants {
  CONDITIONAL_TRUE = 1 ? 7 : (1 / 0),
  CONDITIONAL_FALSE = 0 ? (1 / 0) : 9,
  CONDITIONAL_UNSIGNED = (1 ? -1 : 0u) == 4294967295u,
  CONDITIONAL_CHAR_PROMOTION =
      (1 ? (unsigned char)255 : (short)-1) == 255,
  CONDITIONAL_UNSELECTED_COMMA = 1 ? 67 : (1, 2),
  LOGICAL_AND_UNSELECTED_COMMA = 0 && (1, 2),
  LOGICAL_OR_UNSELECTED_COMMA = 1 || (1, 2),
  NESTED_UNSELECTED_COMMAS =
      1 ? (0 ? (1, 2) : 71) : (3, 4)
};

_Static_assert((1 ? 11 : (1 / 0)) == 11,
               "unselected division is not evaluated");
_Static_assert((0 ? (1 / 0) : 13) == 13,
               "false branch can supply the constant expression");

static int true_scalar = 1 ? 17 : (1 / 0);
static int false_scalar = 0 ? (1 / 0) : 19;
static int unselected_call = 1 ? 61 : runtime_value();
static int conditional_values[] = {
    1 ? 23 : (1 / 0),
    0 ? (1 / 0) : 29
};
static struct {
  int first;
  int second;
} conditional_record = {
    1 ? 31 : (1 / 0),
    0 ? runtime_value() : 37
};

int main(void) {
  static int local_true = 1 ? 41 : (1 / 0);
  static int local_false = 0 ? (1 / 0) : 43;
  int condition_effect = 0;
  int true_effect = 0;
  int false_effect = 0;
  int selected =
      ((condition_effect++, 1)
           ? (true_effect++, 47)
           : (false_effect++, runtime_value()));
  int void_effect = 0;

  assert(CONDITIONAL_TRUE == 7);
  assert(CONDITIONAL_FALSE == 9);
  assert(CONDITIONAL_UNSIGNED == 1);
  assert(CONDITIONAL_CHAR_PROMOTION == 1);
  assert(CONDITIONAL_UNSELECTED_COMMA == 67);
  assert(LOGICAL_AND_UNSELECTED_COMMA == 0);
  assert(LOGICAL_OR_UNSELECTED_COMMA == 1);
  assert(NESTED_UNSELECTED_COMMAS == 71);
  assert(true_scalar == 17);
  assert(false_scalar == 19);
  assert(unselected_call == 61);
  assert(conditional_values[0] == 23);
  assert(conditional_values[1] == 29);
  assert(conditional_record.first == 31);
  assert(conditional_record.second == 37);
  assert(local_true == 41);
  assert(local_false == 43);

  assert(selected == 47);
  assert(condition_effect == 1);
  assert(true_effect == 1);
  assert(false_effect == 0);

  (0 ? set_value(&void_effect, 53) : set_value(&void_effect, 59));
  assert(void_effect == 59);
  return 0;
}
