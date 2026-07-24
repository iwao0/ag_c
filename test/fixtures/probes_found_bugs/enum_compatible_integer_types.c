/* Each enum is compatible with the implementation-selected signed or
 * unsigned integer type, including through pointer and function types. */
#include <assert.h>

enum Positive {
  POSITIVE_ZERO,
  POSITIVE_ONE
};

enum Negative {
  NEGATIVE_ONE = -1,
  NEGATIVE_ZERO
};

enum OtherPositive {
  OTHER_POSITIVE_ZERO,
  OTHER_POSITIVE_ONE
};

enum OtherNegative {
  OTHER_NEGATIVE_ONE = -1,
  OTHER_NEGATIVE_ZERO
};

struct PositiveBits {
  enum Positive value : 2;
};

struct NegativeBits {
  enum Negative value : 2;
};

extern enum Positive compatible_positive_global;
unsigned int compatible_positive_global = POSITIVE_ONE;

extern enum Negative compatible_negative_global;
int compatible_negative_global = NEGATIVE_ONE;

_Static_assert(
    _Generic((enum Positive *)0,
             unsigned int *: 1,
             default: 0),
    "nonnegative enum uses unsigned int compatibility");
_Static_assert(
    _Generic((enum Negative *)0,
             int *: 1,
             default: 0),
    "negative enum uses int compatibility");

static unsigned int read_positive(unsigned int *value) {
  return *value;
}

static int read_negative(int *value) {
  return *value;
}

static unsigned int positive_value(enum Positive value);
static unsigned int positive_value(unsigned int value) {
  return value;
}

static int negative_value(enum Negative value);
static int negative_value(int value) {
  return value;
}

static enum Positive compatible_positive_return(void);
static unsigned int compatible_positive_return(void) {
  return POSITIVE_ONE;
}

static enum Negative compatible_negative_return(void);
static int compatible_negative_return(void) {
  return NEGATIVE_ONE;
}

static enum Positive enum_positive_return(void) {
  return POSITIVE_ONE;
}

static unsigned int integer_positive_return(void) {
  return POSITIVE_ONE;
}

static enum Negative enum_negative_return(void) {
  return NEGATIVE_ONE;
}

static int integer_negative_return(void) {
  return NEGATIVE_ONE;
}

int main(void) {
  enum Positive positive = POSITIVE_ONE;
  enum Negative negative = NEGATIVE_ONE;
  struct PositiveBits positive_bits = {POSITIVE_ONE};
  struct NegativeBits negative_bits = {NEGATIVE_ONE};
  unsigned int *positive_pointer = &positive;
  int *negative_pointer = &negative;
  enum Positive *positive_enum_pointer = &positive;
  enum Negative *negative_enum_pointer = &negative;
  unsigned int **positive_pointer_pointer =
      &positive_enum_pointer;
  int **negative_pointer_pointer = &negative_enum_pointer;
  const unsigned int *const_positive_pointer = &positive;
  const int *const_negative_pointer = &negative;
  enum Positive positive_values[2] = {
    POSITIVE_ZERO, POSITIVE_ONE
  };
  unsigned int (*positive_row)[2] = &positive_values;
  enum Positive *conditional_positive_enum =
      positive ? positive_pointer : &positive;
  unsigned int *conditional_positive_integer =
      positive ? &positive : positive_pointer;
  enum Negative *conditional_negative_enum =
      negative ? negative_pointer : &negative;
  int *conditional_negative_integer =
      negative ? &negative : negative_pointer;
  unsigned int (*conditional_positive_function)(void) =
      positive ? enum_positive_return : integer_positive_return;
  int (*conditional_negative_function)(void) =
      negative ? enum_negative_return : integer_negative_return;

  assert(read_positive(&positive) == 1u);
  assert(read_negative(&negative) == -1);
  assert(_Generic(positive,
                  unsigned int: 1,
                  default: 0));
  assert(_Generic(negative,
                  int: 1,
                  default: 0));
  assert(_Generic(positive,
                  enum Positive: 1,
                  enum OtherPositive: 2,
                  default: 0) == 1);
  assert(_Generic(negative,
                  enum Negative: 1,
                  enum OtherNegative: 2,
                  default: 0) == 1);
  assert(positive_value(positive) == 1u);
  assert(negative_value(negative) == -1);
  assert(compatible_positive_global == POSITIVE_ONE);
  assert(compatible_negative_global == NEGATIVE_ONE);
  assert(compatible_positive_return() == POSITIVE_ONE);
  assert(compatible_negative_return() == NEGATIVE_ONE);
  assert(positive_pointer == &positive);
  assert(negative_pointer == &negative);
  assert(**positive_pointer_pointer == POSITIVE_ONE);
  assert(**negative_pointer_pointer == NEGATIVE_ONE);
  assert(*const_positive_pointer == POSITIVE_ONE);
  assert(*const_negative_pointer == NEGATIVE_ONE);
  assert(conditional_positive_enum == &positive);
  assert(conditional_positive_integer == &positive);
  assert(conditional_negative_enum == &negative);
  assert(conditional_negative_integer == &negative);
  assert(conditional_positive_function() == POSITIVE_ONE);
  assert(conditional_negative_function() == NEGATIVE_ONE);
  assert((*positive_row)[1] == 1u);
  assert(positive_bits.value == POSITIVE_ONE);
  assert(negative_bits.value == NEGATIVE_ONE);

  *positive_pointer = 0u;
  *negative_pointer = 0;
  assert(positive == POSITIVE_ZERO);
  assert(negative == NEGATIVE_ZERO);
  return 0;
}
