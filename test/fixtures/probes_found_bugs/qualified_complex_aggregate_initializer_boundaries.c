// Qualified complex aggregate subobjects still consume one initializer
// expression each.  Preserve const/volatile/_Atomic access semantics while
// applying real-to-complex and cross-width complex conversions.
#include <complex.h>

typedef float complex float_complex;
typedef double complex double_complex;

struct qualified_values {
  int prefix;
  const float_complex immutable;
  volatile double_complex observable;
  _Atomic(float_complex) atomic_narrow;
  _Atomic(double_complex) atomic_wide;
  int suffix;
};

struct atomic_array_holder {
  int prefix;
  _Atomic(float_complex) values[2];
  int suffix;
};

static struct qualified_values global_values = {
  1,
  2.5 + 3.5 * I,
  4.5f + 5.5f * I,
  6.5 + 7.5 * I,
  8.5f + 9.5f * I,
  10,
};

static struct atomic_array_holder global_array = {
  11,
  {
    12.5 + 13.5 * I,
    14.5L + 15.5L * I,
  },
  16,
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

static int float_is(
    float_complex value, float real, float imaginary_part) {
  return crealf(value) == real && cimagf(value) == imaginary_part;
}

static int double_is(
    double_complex value, double real, double imaginary_part) {
  return creal(value) == real && cimag(value) == imaginary_part;
}

static int check_values(
    const struct qualified_values *value,
    int prefix,
    float immutable_real, float immutable_imaginary,
    double observable_real, double observable_imaginary,
    float atomic_narrow_real, float atomic_narrow_imaginary,
    double atomic_wide_real, double atomic_wide_imaginary,
    int suffix) {
  float_complex immutable_snapshot = value->immutable;
  double_complex observable_snapshot = value->observable;
  float_complex atomic_narrow_snapshot = value->atomic_narrow;
  double_complex atomic_wide_snapshot = value->atomic_wide;
  return value->prefix == prefix &&
         float_is(
             immutable_snapshot,
             immutable_real, immutable_imaginary) &&
         double_is(
             observable_snapshot,
             observable_real, observable_imaginary) &&
         float_is(
             atomic_narrow_snapshot,
             atomic_narrow_real, atomic_narrow_imaginary) &&
         double_is(
             atomic_wide_snapshot,
             atomic_wide_real, atomic_wide_imaginary) &&
         value->suffix == suffix;
}

static int check_static_local(void) {
  static struct qualified_values value = {
    17,
    18.5 + 19.5 * I,
    20.5f + 21.5f * I,
    22,
    23.5f,
    24,
  };
  return check_values(
      &value, 17,
      18.5f, 19.5f,
      20.5, 21.5,
      22.0f, 0.0f,
      23.5, 0.0,
      24);
}

int main(void) {
  if (!check_values(
          &global_values, 1,
          2.5f, 3.5f,
          4.5, 5.5,
          6.5f, 7.5f,
          8.5, 9.5,
          10))
    return 1;

  float_complex global_first = global_array.values[0];
  float_complex global_second = global_array.values[1];
  if (global_array.prefix != 11 ||
      !float_is(global_first, 12.5f, 13.5f) ||
      !float_is(global_second, 14.5f, 15.5f) ||
      global_array.suffix != 16)
    return 2;

  if (!check_static_local())
    return 3;

  evaluation_count = 0;
  struct qualified_values local_values = {
    25,
    next_double(26.5, 27.5),
    next_float(28.5f, 29.5f),
    next_double(30.5, 31.5),
    next_float(32.5f, 33.5f),
    34,
  };
  if (evaluation_count != 4 ||
      !check_values(
          &local_values, 25,
          26.5f, 27.5f,
          28.5, 29.5,
          30.5f, 31.5f,
          32.5, 33.5,
          34))
    return 4;

  evaluation_count = 0;
  struct qualified_values designated = {
    .suffix = 43,
    .atomic_wide = next_float(41.5f, 42.5f),
    .atomic_narrow = next_double(39.5, 40.5),
    .observable = next_float(37.5f, 38.5f),
    .immutable = next_double(35.5, 36.5),
    .prefix = 35,
  };
  if (evaluation_count != 4 ||
      !check_values(
          &designated, 35,
          35.5f, 36.5f,
          37.5, 38.5,
          39.5f, 40.5f,
          41.5, 42.5,
          43))
    return 5;

  evaluation_count = 0;
  struct atomic_array_holder local_array = {
    44,
    {
      next_double(45.5, 46.5),
      next_double(47.5, 48.5),
    },
    49,
  };
  float_complex local_first = local_array.values[0];
  float_complex local_second = local_array.values[1];
  if (evaluation_count != 2 ||
      local_array.prefix != 44 ||
      !float_is(local_first, 45.5f, 46.5f) ||
      !float_is(local_second, 47.5f, 48.5f) ||
      local_array.suffix != 49)
    return 6;

  return 0;
}
