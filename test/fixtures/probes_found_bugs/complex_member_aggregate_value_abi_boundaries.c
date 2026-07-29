// Aggregates containing complex members preserve every real and imaginary
// component across homogeneous, mixed, indirect, return, and variadic ABI
// paths.
// Expected: exit=0
#include <assert.h>
#include <complex.h>
#include <stdarg.h>

typedef float complex float_complex;
typedef double complex double_complex;
typedef long double complex long_double_complex;

struct float_complex_pair {
  float_complex first;
  float_complex second;
};

struct double_complex_pair {
  double_complex first;
  double_complex second;
};

struct mixed_complex_values {
  int tag;
  float_complex narrow;
  double_complex regular;
  long_double_complex wide;
  long tail;
};

union complex_choice {
  double_complex value;
  unsigned long long words[2];
};

static const struct float_complex_pair global_float_pair = {
    1.5f + 2.5f * I,
    3.5f + 4.5f * I};
static const struct double_complex_pair global_double_pair = {
    5.25 + 6.75 * I,
    7.25 + 8.75 * I};
static const struct mixed_complex_values global_mixed = {
    11,
    12.5f + 13.5f * I,
    14.25 + 15.75 * I,
    16.5L + 17.5L * I,
    19};
static const union complex_choice global_choice = {
    .value = 20.25 + 21.75 * I};

static int float_is(
    float_complex value, float real, float imaginary_part) {
  return crealf(value) == real &&
         cimagf(value) == imaginary_part;
}

static int double_is(
    double_complex value, double real, double imaginary_part) {
  return creal(value) == real &&
         cimag(value) == imaginary_part;
}

static int long_double_is(
    long_double_complex value,
    long double real, long double imaginary_part) {
  return creall(value) == real &&
         cimagl(value) == imaginary_part;
}

static int check_float_pair(
    struct float_complex_pair value,
    float first_real, float first_imaginary,
    float second_real, float second_imaginary) {
  return float_is(
             value.first, first_real, first_imaginary) &&
         float_is(
             value.second, second_real, second_imaginary);
}

static int check_double_pair(
    struct double_complex_pair value,
    double first_real, double first_imaginary,
    double second_real, double second_imaginary) {
  return double_is(
             value.first, first_real, first_imaginary) &&
         double_is(
             value.second, second_real, second_imaginary);
}

static int check_mixed(
    struct mixed_complex_values value,
    int tag,
    float narrow_real, float narrow_imaginary,
    double regular_real, double regular_imaginary,
    long double wide_real, long double wide_imaginary,
    long tail) {
  return value.tag == tag &&
         float_is(
             value.narrow, narrow_real, narrow_imaginary) &&
         double_is(
             value.regular, regular_real, regular_imaginary) &&
         long_double_is(
             value.wide, wide_real, wide_imaginary) &&
         value.tail == tail;
}

static int check_choice(
    union complex_choice value,
    double real, double imaginary_part) {
  return double_is(value.value, real, imaginary_part);
}

static struct float_complex_pair swap_float_pair(
    struct float_complex_pair value) {
  float_complex first = value.first;
  value.first = value.second;
  value.second = first;
  return value;
}

static struct double_complex_pair swap_double_pair(
    struct double_complex_pair value) {
  double_complex first = value.first;
  value.first = value.second;
  value.second = first;
  return value;
}

static struct mixed_complex_values negate_mixed(
    struct mixed_complex_values value) {
  value.tag++;
  value.narrow = -value.narrow;
  value.regular = -value.regular;
  value.wide = -value.wide;
  value.tail++;
  return value;
}

static union complex_choice negate_choice(
    union complex_choice value) {
  value.value = -value.value;
  return value;
}

typedef struct float_complex_pair float_pair_transform_t(
    struct float_complex_pair);
typedef struct double_complex_pair double_pair_transform_t(
    struct double_complex_pair);
typedef struct mixed_complex_values mixed_transform_t(
    struct mixed_complex_values);
typedef union complex_choice choice_transform_t(
    union complex_choice);

static int check_variadic_complex_aggregates(int marker, ...) {
  va_list arguments;
  va_start(arguments, marker);
  struct float_complex_pair float_pair =
      va_arg(arguments, struct float_complex_pair);
  struct double_complex_pair double_pair =
      va_arg(arguments, struct double_complex_pair);
  struct mixed_complex_values mixed =
      va_arg(arguments, struct mixed_complex_values);
  union complex_choice choice =
      va_arg(arguments, union complex_choice);
  va_end(arguments);

  return marker == 23 &&
         check_float_pair(
             float_pair, 1.5f, 2.5f, 3.5f, 4.5f) &&
         check_double_pair(
             double_pair, 5.25, 6.75, 7.25, 8.75) &&
         check_mixed(
             mixed, 11,
             12.5f, 13.5f,
             14.25, 15.75,
             16.5L, 17.5L,
             19) &&
         check_choice(choice, 20.25, 21.75);
}

static void verify_direct_indirect_and_variadic_calls(void) {
  assert(check_float_pair(
      global_float_pair, 1.5f, 2.5f, 3.5f, 4.5f));
  assert(check_double_pair(
      global_double_pair, 5.25, 6.75, 7.25, 8.75));
  assert(check_mixed(
      global_mixed, 11,
      12.5f, 13.5f,
      14.25, 15.75,
      16.5L, 17.5L,
      19));
  assert(check_choice(global_choice, 20.25, 21.75));

  float_pair_transform_t *float_transform =
      swap_float_pair;
  double_pair_transform_t *double_transform =
      swap_double_pair;
  mixed_transform_t *mixed_transform = negate_mixed;
  choice_transform_t *choice_transform = negate_choice;

  struct float_complex_pair float_result =
      float_transform(global_float_pair);
  struct double_complex_pair double_result =
      double_transform(global_double_pair);
  struct mixed_complex_values mixed_result =
      mixed_transform(global_mixed);
  union complex_choice choice_result =
      choice_transform(global_choice);

  assert(check_float_pair(
      float_result, 3.5f, 4.5f, 1.5f, 2.5f));
  assert(check_double_pair(
      double_result, 7.25, 8.75, 5.25, 6.75));
  assert(check_mixed(
      mixed_result, 12,
      -12.5f, -13.5f,
      -14.25, -15.75,
      -16.5L, -17.5L,
      20));
  assert(check_choice(choice_result, -20.25, -21.75));

  assert(check_variadic_complex_aggregates(
      23, global_float_pair, global_double_pair,
      global_mixed, global_choice));
}

static void verify_assignment_conditionals_and_canaries(void) {
  struct float_complex_pair float_right = {
      24.5f + 25.5f * I,
      26.5f + 27.5f * I};
  struct double_complex_pair double_right = {
      28.25 + 29.75 * I,
      30.25 + 31.75 * I};
  struct mixed_complex_values mixed_right = {
      32,
      33.5f + 34.5f * I,
      35.25 + 36.75 * I,
      37.5L + 38.5L * I,
      39};
  union complex_choice choice_right = {
      .value = 40.25 + 41.75 * I};
  int choose_left = 0;
  int comma_evaluations = 0;

  struct float_complex_pair selected_float =
      choose_left ? global_float_pair : float_right;
  struct double_complex_pair selected_double =
      choose_left ? global_double_pair : double_right;
  struct mixed_complex_values selected_mixed =
      choose_left ? global_mixed : mixed_right;
  union complex_choice selected_choice =
      choose_left ? global_choice : choice_right;
  assert(check_float_pair(
      selected_float, 24.5f, 25.5f, 26.5f, 27.5f));
  assert(check_double_pair(
      selected_double, 28.25, 29.75, 30.25, 31.75));
  assert(check_mixed(
      selected_mixed, 32,
      33.5f, 34.5f,
      35.25, 36.75,
      37.5L, 38.5L,
      39));
  assert(check_choice(selected_choice, 40.25, 41.75));

  struct {
    unsigned char before;
    struct double_complex_pair value;
    unsigned char after;
  } pair_box = {.before = 0x5a, .after = 0xa5};
  struct {
    unsigned char before;
    struct mixed_complex_values value;
    unsigned char after;
  } mixed_box = {.before = 0x3c, .after = 0xc3};

  struct double_complex_pair pair_assignment =
      (pair_box.value = swap_double_pair(global_double_pair));
  struct mixed_complex_values mixed_assignment =
      (mixed_box.value = negate_mixed(global_mixed));
  assert(pair_box.before == 0x5a && pair_box.after == 0xa5);
  assert(mixed_box.before == 0x3c && mixed_box.after == 0xc3);
  assert(check_double_pair(
      pair_assignment, 7.25, 8.75, 5.25, 6.75));
  assert(check_mixed(
      mixed_assignment, 12,
      -12.5f, -13.5f,
      -14.25, -15.75,
      -16.5L, -17.5L,
      20));

  struct float_complex_pair comma_float =
      (comma_evaluations++, global_float_pair);
  struct double_complex_pair comma_double =
      (comma_evaluations++, double_right);
  struct mixed_complex_values comma_mixed =
      (comma_evaluations++, global_mixed);
  union complex_choice comma_choice =
      (comma_evaluations++, choice_right);
  assert(check_float_pair(
      comma_float, 1.5f, 2.5f, 3.5f, 4.5f));
  assert(check_double_pair(
      comma_double, 28.25, 29.75, 30.25, 31.75));
  assert(check_mixed(
      comma_mixed, 11,
      12.5f, 13.5f,
      14.25, 15.75,
      16.5L, 17.5L,
      19));
  assert(check_choice(comma_choice, 40.25, 41.75));
  assert(comma_evaluations == 4);
}

int main(void) {
  verify_direct_indirect_and_variadic_calls();
  verify_assignment_conditionals_and_canaries();
  return 0;
}
