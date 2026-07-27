// A complex subobject is one scalar object even though its static data plan
// uses separate real and imaginary leaves.  Positional aggregate
// initialization must consume one expression per complex object and apply
// the usual arithmetic conversion to both components.
#include <complex.h>

typedef float complex float_complex;
typedef double complex double_complex;
typedef long double complex long_double_complex;

struct three_widths {
  int prefix;
  float_complex narrow;
  double_complex regular;
  long_double_complex wide;
  int suffix;
};

union complex_choice {
  float_complex narrow;
  double_complex regular;
  long_double_complex wide;
};

struct nested_values {
  int prefix;
  struct three_widths rows[2];
  float_complex tail[2];
  int suffix;
};

static struct three_widths global_values = {
  1,
  2.5 + 3.5 * I,
  4.5f + 5.5f * I,
  6.5 + 7.5 * I,
  8,
};

static float_complex global_array[] = {
  9.5 + 10.5 * I,
  11.5L + 12.5L * I,
};

static union complex_choice global_union = {
  13.5 + 14.5 * I,
};

static struct nested_values global_nested = {
  15,
  {
    {
      16,
      17.5 + 18.5 * I,
      19.5f + 20.5f * I,
      21.5 + 22.5 * I,
      23,
    },
    {
      24,
      25.5L + 26.5L * I,
      27.5f + 28.5f * I,
      29.5 + 30.5 * I,
      31,
    },
  },
  {
    32.5 + 33.5 * I,
    34.5L + 35.5L * I,
  },
  36,
};

static int evaluation_count;

static float_complex next_float(
    float real, float imaginary_part) {
  evaluation_count++;
  return real + imaginary_part * I;
}

static double_complex next_double(
    double real, double imaginary_part) {
  evaluation_count++;
  return real + imaginary_part * I;
}

static long_double_complex next_long_double(
    long double real, long double imaginary_part) {
  evaluation_count++;
  return real + imaginary_part * I;
}

static int float_is(
    float_complex value, float real, float imaginary_part) {
  return crealf(value) == real && cimagf(value) == imaginary_part;
}

static int double_is(
    double_complex value, double real, double imaginary_part) {
  return creal(value) == real && cimag(value) == imaginary_part;
}

static int long_double_is(
    long_double_complex value,
    long double real, long double imaginary_part) {
  return creall(value) == real &&
         cimagl(value) == imaginary_part;
}

static int check_three_widths(
    const struct three_widths *value,
    int prefix,
    float narrow_real, float narrow_imaginary,
    double regular_real, double regular_imaginary,
    long double wide_real, long double wide_imaginary,
    int suffix) {
  return value->prefix == prefix &&
         float_is(value->narrow, narrow_real, narrow_imaginary) &&
         double_is(
             value->regular, regular_real, regular_imaginary) &&
         long_double_is(
             value->wide, wide_real, wide_imaginary) &&
         value->suffix == suffix;
}

static int check_static_local(void) {
  static struct three_widths value = {
    37,
    38.5 + 39.5 * I,
    40.5f + 41.5f * I,
    42.5 + 43.5 * I,
    44,
  };
  return check_three_widths(
      &value, 37,
      38.5f, 39.5f,
      40.5, 41.5,
      42.5L, 43.5L,
      44);
}

int main(void) {
  if (!check_three_widths(
          &global_values, 1,
          2.5f, 3.5f,
          4.5, 5.5,
          6.5L, 7.5L,
          8))
    return 1;

  if (sizeof(global_array) / sizeof(global_array[0]) != 2 ||
      !float_is(global_array[0], 9.5f, 10.5f) ||
      !float_is(global_array[1], 11.5f, 12.5f))
    return 2;

  if (!float_is(global_union.narrow, 13.5f, 14.5f))
    return 3;

  if (global_nested.prefix != 15 ||
      !check_three_widths(
          &global_nested.rows[0], 16,
          17.5f, 18.5f,
          19.5, 20.5,
          21.5L, 22.5L,
          23) ||
      !check_three_widths(
          &global_nested.rows[1], 24,
          25.5f, 26.5f,
          27.5, 28.5,
          29.5L, 30.5L,
          31) ||
      !float_is(global_nested.tail[0], 32.5f, 33.5f) ||
      !float_is(global_nested.tail[1], 34.5f, 35.5f) ||
      global_nested.suffix != 36)
    return 4;

  if (!check_static_local())
    return 5;

  evaluation_count = 0;
  struct three_widths local_values = {
    45,
    next_double(46.5, 47.5),
    next_float(48.5f, 49.5f),
    next_double(50.5, 51.5),
    52,
  };
  if (evaluation_count != 3 ||
      !check_three_widths(
          &local_values, 45,
          46.5f, 47.5f,
          48.5, 49.5,
          50.5L, 51.5L,
          52))
    return 6;

  evaluation_count = 0;
  struct three_widths designated = {
    .suffix = 60,
    .wide = next_float(58.5f, 59.5f),
    .regular = next_long_double(56.5L, 57.5L),
    .narrow = next_double(54.5, 55.5),
    .prefix = 53,
  };
  if (evaluation_count != 3 ||
      !check_three_widths(
          &designated, 53,
          54.5f, 55.5f,
          56.5, 57.5,
          58.5L, 59.5L,
          60))
    return 7;

  evaluation_count = 0;
  float_complex local_array[] = {
    next_double(61.5, 62.5),
    next_long_double(63.5L, 64.5L),
  };
  union complex_choice local_union = {
    next_double(65.5, 66.5),
  };
  if (evaluation_count != 3 ||
      sizeof(local_array) / sizeof(local_array[0]) != 2 ||
      !float_is(local_array[0], 61.5f, 62.5f) ||
      !float_is(local_array[1], 63.5f, 64.5f) ||
      !float_is(local_union.narrow, 65.5f, 66.5f))
    return 8;

  evaluation_count = 0;
  struct nested_values local_nested = {
    67,
    {
      {
        68,
        next_double(69.5, 70.5),
        next_float(71.5f, 72.5f),
        next_double(73.5, 74.5),
        75,
      },
      {
        76,
        next_long_double(77.5L, 78.5L),
        next_float(79.5f, 80.5f),
        next_double(81.5, 82.5),
        83,
      },
    },
    {
      next_double(84.5, 85.5),
      next_long_double(86.5L, 87.5L),
    },
    88,
  };
  if (evaluation_count != 8 ||
      local_nested.prefix != 67 ||
      !check_three_widths(
          &local_nested.rows[0], 68,
          69.5f, 70.5f,
          71.5, 72.5,
          73.5L, 74.5L,
          75) ||
      !check_three_widths(
          &local_nested.rows[1], 76,
          77.5f, 78.5f,
          79.5, 80.5,
          81.5L, 82.5L,
          83) ||
      !float_is(local_nested.tail[0], 84.5f, 85.5f) ||
      !float_is(local_nested.tail[1], 86.5f, 87.5f) ||
      local_nested.suffix != 88)
    return 9;

  return 0;
}
