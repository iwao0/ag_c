// C11 assignment conversion turns a real arithmetic value into a complex
// value by converting the real component and supplying a zero imaginary
// component.  Exercise that rule through static/local initialization,
// aggregate leaves, ordinary and atomic assignment, and assignment values.
#include <complex.h>

typedef float _Complex float_complex;
typedef double _Complex double_complex;
typedef long double _Complex long_double_complex;

struct three_widths {
  float_complex narrow;
  double_complex regular;
  long_double_complex wide;
};

union complex_union {
  double_complex value;
  unsigned char bytes[16];
};

static float_complex global_float = (signed char)-2;
static double_complex global_double = 9UL;
static long_double_complex global_long_double = -10.5L;

static struct three_widths global_holder = {
  3,
  4.5f,
  -5.25,
};

static float_complex global_array[] = {
  6,
  -7.5,
};

static union complex_union global_union = {
  8,
};

static int side_effect_count;

static int next_integer(void) {
  side_effect_count++;
  return -11;
}

static int float_is(float_complex value, float expected) {
  return __real__ value == expected && __imag__ value == 0.0f;
}

static int double_is(double_complex value, double expected) {
  return __real__ value == expected && __imag__ value == 0.0;
}

static int long_double_is(
    long_double_complex value, long double expected) {
  return __real__ value == expected && __imag__ value == 0.0L;
}

int main(void) {
  if (!float_is(global_float, -2.0f) ||
      !double_is(global_double, 9.0) ||
      !long_double_is(global_long_double, -10.5L))
    return 1;

  if (!float_is(global_holder.narrow, 3.0f))
    return 21;
  if (!double_is(global_holder.regular, 4.5))
    return 22;
  if (!long_double_is(global_holder.wide, -5.25L))
    return 23;

  if (!float_is(global_array[0], 6.0f) ||
      !float_is(global_array[1], -7.5f) ||
      !double_is(global_union.value, 8.0))
    return 3;

  float_complex local_float = (unsigned short)12;
  double_complex local_double = -13.5f;
  long_double_complex local_long_double = 14LL;
  if (!float_is(local_float, 12.0f) ||
      !double_is(local_double, -13.5) ||
      !long_double_is(local_long_double, 14.0L))
    return 4;

  struct three_widths local_holder = {
    (_Bool)1,
    (unsigned long long)15,
    16.5f,
  };
  float_complex local_array[2] = {
    (signed short)-17,
    18.5L,
  };
  union complex_union local_union = {
    (unsigned char)19,
  };
  if (!float_is(local_holder.narrow, 1.0f) ||
      !double_is(local_holder.regular, 15.0) ||
      !long_double_is(local_holder.wide, 16.5L) ||
      !float_is(local_array[0], -17.0f) ||
      !float_is(local_array[1], 18.5f) ||
      !double_is(local_union.value, 19.0))
    return 5;

  local_float = (signed int)-20;
  local_double = 21.5L;
  local_long_double = (unsigned int)22;
  if (!float_is(local_float, -20.0f) ||
      !double_is(local_double, 21.5) ||
      !long_double_is(local_long_double, 22.0L))
    return 6;

  side_effect_count = 0;
  double_complex assignment_value =
      (local_double = next_integer());
  if (!double_is(local_double, -11.0) ||
      !double_is(assignment_value, -11.0) ||
      side_effect_count != 1)
    return 7;

  struct three_widths assigned = {0, 0, 0};
  struct three_widths *assigned_pointer = &assigned;
  assigned_pointer->narrow = 23.5;
  assigned.regular = (signed long)-24;
  assigned.wide = 25.5f;
  if (!float_is(assigned.narrow, 23.5f) ||
      !double_is(assigned.regular, -24.0) ||
      !long_double_is(assigned.wide, 25.5L))
    return 8;

  float_complex assigned_array[2] = {0, 0};
  float_complex *element = &assigned_array[1];
  assigned_array[0] = 26;
  *element = -27.5;
  if (!float_is(assigned_array[0], 26.0f) ||
      !float_is(assigned_array[1], -27.5f))
    return 9;

  double_complex chain_left = 1.0 * I;
  double_complex chain_right = 2.0 * I;
  chain_left = chain_right = 28.5f;
  if (!double_is(chain_left, 28.5) ||
      !double_is(chain_right, 28.5))
    return 10;

  volatile float_complex volatile_value = 0;
  volatile_value = (signed char)-29;
  float_complex volatile_snapshot = volatile_value;
  if (!float_is(volatile_snapshot, -29.0f))
    return 11;

  _Atomic(float_complex) atomic_float = 0;
  _Atomic(double_complex) atomic_double = 0;
  atomic_float = (unsigned char)30;
  atomic_double = -31.5f;
  float_complex atomic_float_snapshot = atomic_float;
  double_complex atomic_double_snapshot = atomic_double;
  if (!float_is(atomic_float_snapshot, 30.0f) ||
      !double_is(atomic_double_snapshot, -31.5))
    return 12;

  double_complex atomic_assignment_value =
      (atomic_double = (unsigned long)32);
  atomic_double_snapshot = atomic_double;
  if (!double_is(atomic_assignment_value, 32.0) ||
      !double_is(atomic_double_snapshot, 32.0))
    return 13;

  return 0;
}
