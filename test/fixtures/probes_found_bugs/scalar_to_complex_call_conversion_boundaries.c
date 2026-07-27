// C11 assignment conversion applies to each prototyped function argument.
// Every real arithmetic type can therefore initialize a complex parameter:
// the converted real part is preserved and the imaginary part becomes zero.
// Cover direct and indirect calls, mixed ABI positions, and single evaluation.
typedef float _Complex float_complex;
typedef double _Complex double_complex;
typedef long double _Complex long_double_complex;

typedef int (*double_checker)(double_complex, double);

struct checker_holder {
  double_checker check;
};

static int double_call_count;
static int integer_call_count;

static double next_double(void) {
  double_call_count++;
  return 6.5;
}

static int next_integer(void) {
  integer_call_count++;
  return -7;
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

static long double three_width_sum(
    float_complex narrow,
    double_complex regular,
    long_double_complex wide) {
  if (__imag__ narrow != 0.0f ||
      __imag__ regular != 0.0 ||
      __imag__ wide != 0.0L)
    return 1000.0L;
  return __real__ narrow + __real__ regular + __real__ wide;
}

static double mixed_positions(
    int prefix, double_complex value, int suffix) {
  if (__imag__ value != 0.0)
    return 1000.0;
  return prefix + __real__ value + suffix;
}

int main(void) {
  if (!float_is((_Bool)1, 1.0f))
    return 1;
  if (!float_is((signed char)-2, -2.0f))
    return 2;
  if (!float_is(3, 3.0f))
    return 3;
  if (!float_is(4.25f, 4.25f))
    return 4;

  if (!double_is((unsigned short)5, 5.0) ||
      !double_is(6L, 6.0) ||
      !double_is(7.5f, 7.5) ||
      !double_is(-8.25, -8.25))
    return 5;

  if (!long_double_is(9U, 9.0L) ||
      !long_double_is(-10LL, -10.0L) ||
      !long_double_is(11.5, 11.5L) ||
      !long_double_is(-12.75L, -12.75L))
    return 6;

  double_checker pointer = double_is;
  struct checker_holder holder = {double_is};
  if (!pointer(13, 13.0) ||
      !holder.check(14.5f, 14.5))
    return 7;

  if (three_width_sum(2, 3.5f, -4.0L) != 1.5L)
    return 8;

  double (*mixed_pointer)(int, double_complex, int) =
      mixed_positions;
  if (mixed_positions(10, 2.5f, 20) != 32.5 ||
      mixed_pointer(30, -3, 40) != 67.0)
    return 9;

  double_call_count = 0;
  if (!double_is(next_double(), 6.5) ||
      double_call_count != 1)
    return 10;

  integer_call_count = 0;
  if (!pointer(next_integer(), -7.0) ||
      integer_call_count != 1)
    return 11;

  return 0;
}
