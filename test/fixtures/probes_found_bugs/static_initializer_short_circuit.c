#include <assert.h>

_Static_assert((0 && (1 / 0)) == 0, "short-circuit and");
_Static_assert((1 || (1 / 0)) == 1, "short-circuit or");

enum short_circuit_constants {
  ENUM_SHORT_AND = 0 && (1 / 0),
  ENUM_SHORT_OR = 1 || (1 / 0)
};

static int short_and = 0 && (1 / 0);
static int short_or = 1 || (1 / 0);
static int short_values[] = {
  0 && (1 / 0),
  1 || (1 / 0)
};
static struct {
  int and_value;
  int or_value;
} short_record = {
  0 && (1 / 0),
  1 || (1 / 0)
};

int main(void) {
  static int local_short_and = 0 && (1 / 0);
  static int local_short_or = 1 || (1 / 0);

  assert(ENUM_SHORT_AND == 0);
  assert(ENUM_SHORT_OR == 1);
  assert(short_and == 0);
  assert(short_or == 1);
  assert(short_values[0] == 0);
  assert(short_values[1] == 1);
  assert(short_record.and_value == 0);
  assert(short_record.or_value == 1);
  assert(local_short_and == 0);
  assert(local_short_or == 1);
  return 0;
}
