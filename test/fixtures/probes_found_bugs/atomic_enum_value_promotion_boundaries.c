// Atomic enum lvalue conversion feeds the enum's compatible integer
// promotion into ordinary, conditional, comma, and variadic value contexts.
// Expected: exit=0
#include <stdarg.h>

enum signed_state {
  SIGNED_STATE_NEGATIVE = -1,
  SIGNED_STATE_VALUE = 17
};

enum unsigned_state {
  UNSIGNED_STATE_ZERO = 0,
  UNSIGNED_STATE_VALUE = 25
};

static _Atomic(enum signed_state) signed_value =
    SIGNED_STATE_VALUE;
static _Atomic(enum unsigned_state) unsigned_value =
    UNSIGNED_STATE_VALUE;

static int signed_selects;
static int unsigned_selects;
static int comma_evaluations;

static int check_unprototyped();

static _Atomic(enum signed_state) *select_signed(void) {
  signed_selects++;
  return &signed_value;
}

static _Atomic(enum unsigned_state) *select_unsigned(void) {
  unsigned_selects++;
  return &unsigned_value;
}

static int read_signed(enum signed_state value) {
  return value;
}

static unsigned int read_unsigned(unsigned int value) {
  return value;
}

static int check_promoted_values(int marker, ...) {
  va_list arguments;
  int direct_signed;
  unsigned int direct_unsigned;
  int selected_signed;
  unsigned int selected_unsigned;
  int comma_signed;
  unsigned int comma_unsigned;

  va_start(arguments, marker);
  direct_signed = va_arg(arguments, int);
  direct_unsigned = va_arg(arguments, unsigned int);
  selected_signed = va_arg(arguments, int);
  selected_unsigned = va_arg(arguments, unsigned int);
  comma_signed = va_arg(arguments, int);
  comma_unsigned = va_arg(arguments, unsigned int);
  va_end(arguments);

  if (marker != 101)
    return 1;
  if (direct_signed != 17 || direct_unsigned != 25U)
    return 2;
  if (selected_signed != 17 || selected_unsigned != 25U)
    return 3;
  if (comma_signed != 17 || comma_unsigned != 25U)
    return 4;
  return 0;
}

int main(void) {
  enum signed_state comma_signed;
  enum unsigned_state comma_unsigned;
  int (*unprototyped_callback)() = check_unprototyped;
  int status;

  if (read_signed(*select_signed()) != 17)
    return 5;
  if (read_unsigned(*select_unsigned()) != 25U)
    return 6;

  comma_signed = (comma_evaluations++, signed_value);
  comma_unsigned = (comma_evaluations++, unsigned_value);
  status = check_promoted_values(
      101,
      signed_value,
      unsigned_value,
      1 ? signed_value : SIGNED_STATE_NEGATIVE,
      1 ? unsigned_value : 0U,
      comma_signed,
      comma_unsigned);
  if (status != 0)
    return status;
  if (signed_selects != 1 || unsigned_selects != 1)
    return 7;
  if (comma_evaluations != 2)
    return 8;
  if (check_unprototyped(signed_value, unsigned_value) != 0)
    return 9;
  if (unprototyped_callback(signed_value, unsigned_value) != 0)
    return 10;
  return 0;
}

static int check_unprototyped(signed_argument, unsigned_argument)
int signed_argument;
unsigned int unsigned_argument;
{
  if (signed_argument != 17)
    return 1;
  if (unsigned_argument != 25U)
    return 2;
  return 0;
}
