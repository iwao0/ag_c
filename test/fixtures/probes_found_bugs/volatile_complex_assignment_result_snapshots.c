/*
 * A complex assignment expression yields the value computed for the store.
 * Using that value must not reload a volatile destination after the write.
 */
#include <assert.h>
#include <complex.h>

typedef float complex float_complex;
typedef double complex double_complex;
typedef long double complex long_double_complex;

struct complex_holder {
  volatile double_complex value;
  int marker;
};

static volatile double_complex sources[4] = {
    CMPLX(3.25, -4.5),
    CMPLX(5.75, -6.25),
    CMPLX(7.5, -8.75),
    CMPLX(9.25, -10.5),
};
static volatile double_complex targets[4] = {
    CMPLX(1.0, 2.0),
    CMPLX(3.0, 4.0),
    CMPLX(5.0, 6.0),
    CMPLX(7.0, 8.0),
};
static volatile double_complex * volatile active_target = &targets[2];
static struct complex_holder holder = {
    CMPLX(11.5, -12.75),
    13,
};

static volatile float_complex add_target = CMPLXF(1.5f, 2.5f);
static volatile float_complex add_source = CMPLXF(3.0f, -0.5f);
static volatile double_complex subtract_target = CMPLX(7.0, 4.0);
static volatile double_complex subtract_source = CMPLX(2.0, 1.0);
static volatile long_double_complex multiply_target =
    CMPLXL(2.0L, 3.0L);
static volatile long_double_complex multiply_source =
    CMPLXL(4.0L, 0.0L);
static volatile long_double_complex divide_target =
    CMPLXL(8.0L, 12.0L);
static volatile long_double_complex divide_source =
    CMPLXL(4.0L, 0.0L);

static int target_calls;
static int source_calls;
static int scalar_calls;

static volatile double_complex *select_target(
    volatile double_complex *pointer) {
  target_calls++;
  return pointer;
}

static volatile double_complex *select_source(
    volatile double_complex *pointer) {
  source_calls++;
  return pointer;
}

static double next_scalar(double value) {
  scalar_calls++;
  return value;
}

static int float_is(
    float_complex value, float expected_real,
    float expected_imaginary) {
  return crealf(value) == expected_real &&
         cimagf(value) == expected_imaginary;
}

static int double_is(
    double_complex value, double expected_real,
    double expected_imaginary) {
  return creal(value) == expected_real &&
         cimag(value) == expected_imaginary;
}

static int long_double_is(
    long_double_complex value, long double expected_real,
    long double expected_imaginary) {
  return creall(value) == expected_real &&
         cimagl(value) == expected_imaginary;
}

int main(void) {
  volatile float_complex local_target = CMPLXF(1.0f, -2.0f);
  volatile float_complex local_source = CMPLXF(13.5f, -14.25f);

  float_complex local_result = (local_target = local_source);
  assert(float_is(local_result, 13.5f, -14.25f));
  assert(float_is(local_target, 13.5f, -14.25f));

  double_complex direct_result = (targets[0] = sources[0]);
  assert(double_is(direct_result, 3.25, -4.5));
  assert(double_is(targets[0], 3.25, -4.5));

  target_calls = 0;
  source_calls = 0;
  double_complex selected_result =
      (*select_target(&targets[1]) =
       *select_source(&sources[1]));
  assert(target_calls == 1);
  assert(source_calls == 1);
  assert(double_is(selected_result, 5.75, -6.25));
  assert(double_is(targets[1], 5.75, -6.25));

  source_calls = 0;
  double_complex pointer_result =
      (*active_target = *select_source(&sources[2]));
  assert(source_calls == 1);
  assert(active_target == &targets[2]);
  assert(double_is(pointer_result, 7.5, -8.75));
  assert(double_is(targets[2], 7.5, -8.75));

  target_calls = 0;
  scalar_calls = 0;
  double_complex scalar_result =
      (*select_target(&targets[3]) = next_scalar(23.5));
  assert(target_calls == 1);
  assert(scalar_calls == 1);
  assert(double_is(scalar_result, 23.5, 0.0));
  assert(double_is(targets[3], 23.5, 0.0));

  double_complex member_result = (holder.value = sources[3]);
  assert(double_is(member_result, 9.25, -10.5));
  assert(double_is(holder.value, 9.25, -10.5));
  assert(holder.marker == 13);

  float_complex add_result = (add_target += add_source);
  assert(float_is(add_result, 4.5f, 2.0f));
  assert(float_is(add_target, 4.5f, 2.0f));

  double_complex subtract_result =
      (subtract_target -= subtract_source);
  assert(double_is(subtract_result, 5.0, 3.0));
  assert(double_is(subtract_target, 5.0, 3.0));

  long_double_complex multiply_result =
      (multiply_target *= multiply_source);
  assert(long_double_is(multiply_result, 8.0L, 12.0L));
  assert(long_double_is(multiply_target, 8.0L, 12.0L));

  long_double_complex divide_result =
      (divide_target /= divide_source);
  assert(long_double_is(divide_result, 2.0L, 3.0L));
  assert(long_double_is(divide_target, 2.0L, 3.0L));

  target_calls = 0;
  source_calls = 0;
  double_complex selected_compound_result =
      (*select_target(&targets[0]) +=
       *select_source(&sources[1]));
  assert(target_calls == 1);
  assert(source_calls == 1);
  assert(double_is(selected_compound_result, 9.0, -10.75));
  assert(double_is(targets[0], 9.0, -10.75));
  return 0;
}
