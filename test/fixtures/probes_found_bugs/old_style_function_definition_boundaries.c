#include <assert.h>
#include <complex.h>

struct Pair {
  int left;
  int right;
};

typedef struct Pair atomic_pair_callback_t(
    _Atomic(struct Pair));

struct atomic_pair_callback_holder {
  atomic_pair_callback_t *callback;
};

union Number {
  int integer;
  float floating;
};

typedef unsigned short small_t;
typedef _Atomic short atomic_short_t;
typedef int atomic_callback_t(_Atomic int);
typedef int plain_callback_t(int);

enum signed_code {
  SIGNED_CODE_NEGATIVE = -1,
  SIGNED_CODE_VALUE = 41
};

enum unsigned_code {
  UNSIGNED_CODE_ZERO = 0,
  UNSIGNED_CODE_VALUE = 42
};

int narrow_char(value)
char value;
{
  assert(sizeof value == 1);
  return value;
}

int narrow_unsigned(value)
unsigned char value;
{
  assert(sizeof value == 1);
  return value;
}

int narrow_short(value)
small_t value;
{
  assert(sizeof value == 2);
  return value;
}

int qualified_char(value)
const signed char value;
{
  assert(sizeof value == 1);
  return value;
}

double narrow_float(value)
float value;
{
  assert(sizeof value == 4);
  return value + 0.25f;
}

int sum_array(values, count)
int count;
register int values[];
{
  int sum = 0;
  int index;
  for (index = 0; index < count; index++)
    sum += values[index];
  return sum;
}

int static_array_sum(values)
const int values[static 3];
{
  return values[0] + values[1] + values[2];
}

/* Qualifiers written inside [] apply to the adjusted pointer itself. The
 * definition therefore remains compatible with this unqualified prototype. */
int qualified_array_sum(int *values);

int qualified_array_sum(values)
int values[const restrict static 3];
{
  return values[0] + values[1] + values[2];
}

/* A function parameter declaration adjusts to the corresponding pointer
 * type before the old-style definition is compared with this prototype. */
int apply(int (*callback)(int), int value);

int apply(callback, value)
int callback(int);
int value;
{
  return callback(value);
}

int apply_variadic(int (*callback)(int, ...), int value);
int apply_variadic(callback, value)
int callback(int, ...);
int value;
{
  return callback(value, 1);
}

int add_one(int value) {
  return value + 1;
}

int add_one_variadic(int value, ...) {
  return value + 1;
}

int variadic_prefix(int first, ...);
int variadic_prefix(first)
int first;
{
  return first;
}

int pair_sum(struct Pair pair);
int pair_sum(pair)
struct Pair pair;
{
  return pair.left + pair.right;
}

int integer_value(union Number value);
int integer_value(value)
union Number value;
{
  return value.integer;
}

int sum_pair(left, right)
int left, right;
{
  return left + right;
}

int same_declaration_vla(values, count)
int count, values[count];
{
  return values[count - 1];
}

int same_declaration_matrix(matrix, rows, columns)
int rows, columns, matrix[rows][columns];
{
  return matrix[rows - 1][columns - 1];
}

int compatible_integer(int, int);
int compatible_integer(left, right)
char left;
short right;
{
  return left + right;
}

int compatible_subinteger(int, int);
int compatible_subinteger(left, right)
_Bool left;
unsigned short right;
{
  return left + right;
}

double compatible_float(double);
double compatible_float(value)
float value;
{
  return value;
}

int compatible_float_complex(float _Complex);
int compatible_float_complex(value)
float _Complex value;
{
  return crealf(value) == 2.0f && cimagf(value) == 3.0f;
}

int compatible_const(int);
int compatible_const(value)
const int value;
{
  return value;
}

int compatible_signed_enum(int);
int compatible_signed_enum(value)
enum signed_code value;
{
  return value;
}

int compatible_unsigned_enum(unsigned int);
int compatible_unsigned_enum(value)
enum unsigned_code value;
{
  return value;
}

int compatible_atomic_int(_Atomic int);
int compatible_atomic_int(value)
_Atomic int value;
{
  return value;
}

int compatible_atomic_short(atomic_short_t);
int compatible_atomic_short(value)
atomic_short_t value;
{
  return value;
}

int compatible_atomic_pointer(int * _Atomic);
int compatible_atomic_pointer(value)
int * _Atomic value;
{
  return *value;
}

int compatible_atomic_array(int value[const _Atomic 1]);
int compatible_atomic_array(value)
int value[const _Atomic 1];
{
  return value[0];
}

int decrement_register_atomic(value)
register _Atomic int value;
{
  return --value;
}

struct Pair reverse_register_atomic_pair(
    _Atomic(struct Pair));
struct Pair reverse_register_atomic_pair(value)
register _Atomic(struct Pair) value;
{
  struct Pair snapshot = value;
  value = (struct Pair){snapshot.right, snapshot.left};
  return value;
}

static atomic_pair_callback_t *select_atomic_pair_callback(void) {
  return reverse_register_atomic_pair;
}

static atomic_pair_callback_t *global_atomic_pair_callback =
    reverse_register_atomic_pair;
static atomic_pair_callback_t *global_atomic_pair_callbacks[2] = {
    reverse_register_atomic_pair,
    reverse_register_atomic_pair,
};
static struct atomic_pair_callback_holder
    global_atomic_pair_callback_holder = {
        reverse_register_atomic_pair,
    };

int main(void) {
  int values[] = {3, 5, 7};
  int matrix[2][3] = {
      {11, 13, 17},
      {19, 23, 29},
  };
  struct Pair pair = {11, 13};
  struct Pair reversed_atomic_pair =
      reverse_register_atomic_pair((struct Pair){17, 19});
  atomic_pair_callback_t *atomic_pair_callback =
      reverse_register_atomic_pair;
  struct Pair callback_atomic_pair =
      atomic_pair_callback((struct Pair){23, 29});
  struct Pair returned_callback_atomic_pair =
      select_atomic_pair_callback()((struct Pair){31, 37});
  static atomic_pair_callback_t *static_atomic_pair_callback =
      reverse_register_atomic_pair;
  struct Pair global_callback_atomic_pair =
      global_atomic_pair_callback((struct Pair){41, 43});
  struct Pair array_callback_atomic_pair =
      global_atomic_pair_callbacks[1]((struct Pair){47, 53});
  struct Pair member_callback_atomic_pair =
      global_atomic_pair_callback_holder.callback(
          (struct Pair){59, 61});
  struct Pair static_callback_atomic_pair =
      static_atomic_pair_callback((struct Pair){67, 71});
  union Number number = {31};
  atomic_callback_t *atomic_callback = compatible_atomic_int;
  plain_callback_t *plain_callback = compatible_const;
  assert(narrow_char(257) == 1);
  assert(narrow_unsigned(513) == 1);
  assert(narrow_short(65537) == 1);
  assert(qualified_char(257) == 1);
  assert(narrow_float(1.5f) == 1.75);
  assert(sum_array(values, 3) == 15);
  assert(static_array_sum(values) == 15);
  assert(qualified_array_sum(values) == 15);
  assert(apply(add_one, 41) == 42);
  assert(apply_variadic(add_one_variadic, 41) == 42);
  assert(variadic_prefix(42, 1, 2) == 42);
  assert(pair_sum(pair) == 24);
  assert(reversed_atomic_pair.left == 19 &&
         reversed_atomic_pair.right == 17);
  assert(callback_atomic_pair.left == 29 &&
         callback_atomic_pair.right == 23);
  assert(returned_callback_atomic_pair.left == 37 &&
         returned_callback_atomic_pair.right == 31);
  assert(global_callback_atomic_pair.left == 43 &&
         global_callback_atomic_pair.right == 41);
  assert(array_callback_atomic_pair.left == 53 &&
         array_callback_atomic_pair.right == 47);
  assert(member_callback_atomic_pair.left == 61 &&
         member_callback_atomic_pair.right == 59);
  assert(static_callback_atomic_pair.left == 71 &&
         static_callback_atomic_pair.right == 67);
  assert(integer_value(number) == 31);
  assert(sum_pair(17, 19) == 36);
  assert(same_declaration_vla(values, 3) == 7);
  assert(same_declaration_matrix(matrix, 2, 3) == 29);
  assert(compatible_integer(257, 65538) == 3);
  assert(compatible_subinteger(7, 41) == 42);
  assert(compatible_float(2.5f) == 2.5);
  assert(compatible_float_complex(2.0f + 3.0f * I));
  assert(compatible_const(23) == 23);
  assert(compatible_signed_enum(SIGNED_CODE_VALUE) == 41);
  assert(compatible_unsigned_enum(UNSIGNED_CODE_VALUE) == 42);
  assert(compatible_atomic_int(37) == 37);
  assert(compatible_atomic_short(19) == 19);
  assert(compatible_atomic_pointer(values) == 3);
  assert(compatible_atomic_array(values) == 3);
  assert(decrement_register_atomic(43) == 42);
  assert(atomic_callback(29) == 29);
  assert(_Generic(atomic_callback,
      atomic_callback_t *: 1,
      plain_callback_t *: 2) == 1);
  assert(_Generic(plain_callback,
      atomic_callback_t *: 1,
      plain_callback_t *: 2) == 2);
  return 0;
}
