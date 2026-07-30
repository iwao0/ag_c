// Default argument promotions and value decay through an unprototyped
// function pointer calling an old-style definition.
// Expected: exit=0
#include <complex.h>
#include <limits.h>

enum code {
  CODE_NEGATIVE = -3,
  CODE_POSITIVE = 3
};

struct bits {
  signed int signed_narrow : 5;
  unsigned int unsigned_narrow : 5;
  unsigned int unsigned_full : sizeof(unsigned int) * CHAR_BIT;
  _Bool boolean : 1;
  enum code enumeration : 4;
};

struct small {
  int value;
};

struct wide {
  long long left;
  long long right;
};

typedef int unprototyped_callback_t();

static int check_values();

static int add_one(int value) {
  return value + 1;
}

static int values[3] = {5, 7, 11};
static unprototyped_callback_t *global_callback = check_values;
static unprototyped_callback_t *callback_array[2] = {
    check_values,
    check_values,
};

struct callback_holder {
  unprototyped_callback_t *callback;
};

static struct callback_holder callback_holder = {
    check_values,
};

static int selector_evaluations;

static unprototyped_callback_t *select_callback(void) {
  selector_evaluations++;
  return check_values;
}

static int invoke(unprototyped_callback_t *callback) {
  struct bits bit_values = {
      .signed_narrow = -7,
      .unsigned_narrow = 29,
      .unsigned_full = UINT_MAX,
      .boolean = 1,
      .enumeration = CODE_NEGATIVE,
  };
  float floating = 13.25f;
  float complex complex_value = CMPLXF(17.0f, -19.0f);
  struct small small_value = {23};
  struct wide wide_value = {29, 31};
  return callback(
      101, bit_values.signed_narrow,
      bit_values.unsigned_narrow, bit_values.unsigned_full,
      bit_values.boolean, bit_values.enumeration,
      floating, values, add_one, complex_value,
      small_value, wide_value);
}

int main(void) {
  if (invoke(global_callback) != 0)
    return 20;
  if (invoke(callback_array[1]) != 0)
    return 21;
  if (invoke(callback_holder.callback) != 0)
    return 22;
  if (invoke(select_callback()) != 0)
    return 23;
  if (selector_evaluations != 1)
    return 24;
  return 0;
}

static int check_values(
    marker, signed_narrow, unsigned_narrow, unsigned_full,
    boolean_value, enumeration, floating, array_pointer,
    function_pointer, complex_value, small_value, wide_value)
int marker;
int signed_narrow;
int unsigned_narrow;
unsigned int unsigned_full;
int boolean_value;
int enumeration;
double floating;
int *array_pointer;
int (*function_pointer)(int);
float complex complex_value;
struct small small_value;
struct wide wide_value;
{
  if (marker != 101)
    return 1;
  if (signed_narrow != -7 || unsigned_narrow != 29 ||
      unsigned_full != UINT_MAX)
    return 2;
  if (boolean_value != 1 || enumeration != CODE_NEGATIVE)
    return 3;
  if (floating != 13.25)
    return 4;
  if (array_pointer != values || array_pointer[2] != 11)
    return 5;
  if (function_pointer(41) != 42)
    return 6;
  if (crealf(complex_value) != 17.0f ||
      cimagf(complex_value) != -19.0f)
    return 7;
  if (small_value.value != 23)
    return 8;
  if (wide_value.left != 29 || wide_value.right != 31)
    return 9;
  return 0;
}
