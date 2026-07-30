/*
 * Converting a volatile complex lvalue to a discarded value still reads the
 * complete complex object.  The source access must not be replaced by merely
 * retaining its address.
 */
#include <assert.h>
#include <complex.h>

typedef float complex float_complex;
typedef double complex double_complex;
typedef long double complex long_double_complex;

struct complex_fields {
  volatile float_complex member;
};

static volatile double_complex global_values[2] = {
    CMPLX(3.25, -4.5),
    CMPLX(5.75, -6.25),
};
static int selector_calls;

static volatile double_complex *select_value(
    volatile double_complex *pointer) {
  selector_calls++;
  return pointer;
}

static int double_is(
    double_complex value, double expected_real,
    double expected_imaginary) {
  return creal(value) == expected_real &&
         cimag(value) == expected_imaginary;
}

static int float_is(
    float_complex value, float expected_real,
    float expected_imaginary) {
  return crealf(value) == expected_real &&
         cimagf(value) == expected_imaginary;
}

static int long_double_is(
    long_double_complex value, long double expected_real,
    long double expected_imaginary) {
  return creall(value) == expected_real &&
         cimagl(value) == expected_imaginary;
}

int main(void) {
  volatile float_complex local = CMPLXF(7.5f, -8.25f);
  volatile float_complex *pointer = &local;
  volatile double_complex * volatile volatile_pointer =
      &global_values[0];
  struct complex_fields fields = {
      CMPLXF(9.5f, -10.75f),
  };
  volatile long_double_complex wide =
      CMPLXL(11.25L, -12.5L);

  (void)global_values[1];
  (void)local;
  (void)*pointer;
  (void)*volatile_pointer;
  (void)fields.member;
  (void)wide;
  (void)(volatile float_complex){
      CMPLXF(13.5f, -14.25f),
  };

  selector_calls = 0;
  (void)*select_value(&global_values[0]);
  assert(selector_calls == 1);

  assert(double_is(global_values[0], 3.25, -4.5));
  assert(double_is(global_values[1], 5.75, -6.25));
  assert(float_is(local, 7.5f, -8.25f));
  assert(float_is(*pointer, 7.5f, -8.25f));
  assert(double_is(*volatile_pointer, 3.25, -4.5));
  assert(float_is(fields.member, 9.5f, -10.75f));
  assert(long_double_is(wide, 11.25L, -12.5L));
  return 0;
}
