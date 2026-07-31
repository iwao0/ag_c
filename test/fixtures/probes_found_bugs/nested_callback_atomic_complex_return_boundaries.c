#include <assert.h>
#include <complex.h>
#include <stdatomic.h>

#define CLASSIFY_CONSUMER(expression) \
  _Generic((expression), \
           plain_float_consumer_function *: 1, \
           atomic_float_consumer_function *: 2, \
           plain_double_consumer_function *: 3, \
           atomic_double_consumer_function *: 4, \
           default: 0)

struct guarded_float_complex {
  unsigned int before;
  _Atomic(float complex) value;
  unsigned int after;
};

struct guarded_double_complex {
  unsigned long long before;
  _Atomic(double complex) value;
  unsigned long long after;
};

typedef float complex plain_float_callback_function(void);
typedef _Atomic(float complex) atomic_float_callback_function(void);
typedef double complex plain_double_callback_function(void);
typedef _Atomic(double complex) atomic_double_callback_function(void);

typedef int plain_float_consumer_function(
    plain_float_callback_function *callback);
typedef int atomic_float_consumer_function(
    atomic_float_callback_function *callback);
typedef int plain_double_consumer_function(
    plain_double_callback_function *callback);
typedef int atomic_double_consumer_function(
    atomic_double_callback_function *callback);

_Static_assert(sizeof(_Atomic(float complex)) == 8,
               "atomic float complex storage size");
_Static_assert(sizeof(_Atomic(double complex)) == 16,
               "atomic double complex storage size");

#ifdef __wasm32__
_Static_assert(_Alignof(_Atomic(double complex)) == 8,
               "wasm32 atomic double complex alignment");
#else
_Static_assert(_Alignof(_Atomic(double complex)) == 16,
               "native atomic double complex alignment");
#endif

static float complex plain_float_callback(void) {
  return CMPLXF(19.5f, 22.5f);
}

static _Atomic(float complex) atomic_float_callback(void) {
  return CMPLXF(17.5f, 24.5f);
}

static double complex plain_double_callback(void) {
  return CMPLX(13.25, 28.75);
}

static _Atomic(double complex) atomic_double_callback(void) {
  return CMPLX(11.5, 30.5);
}

static int consume_plain_float(
    plain_float_callback_function *callback) {
  float complex value = callback();

  assert(crealf(value) == 19.5f);
  assert(cimagf(value) == 22.5f);
  return (int)(crealf(value) + cimagf(value));
}

static int consume_atomic_float(
    atomic_float_callback_function *callback) {
  struct guarded_float_complex frame;
  float complex snapshot;

  frame.before = 0x11223344u;
  frame.after = 0x55667788u;
  atomic_init(&frame.value, CMPLXF(0.0f, 0.0f));
  frame.value = callback();
  snapshot = atomic_load(&frame.value);

  assert(frame.before == 0x11223344u);
  assert(frame.after == 0x55667788u);
  assert(crealf(snapshot) == 17.5f);
  assert(cimagf(snapshot) == 24.5f);
  return (int)(crealf(snapshot) + cimagf(snapshot));
}

static int consume_plain_double(
    plain_double_callback_function *callback) {
  double complex value = callback();

  assert(creal(value) == 13.25);
  assert(cimag(value) == 28.75);
  return (int)(creal(value) + cimag(value));
}

static int consume_atomic_double(
    atomic_double_callback_function *callback) {
  struct guarded_double_complex frame;
  double complex snapshot;

  frame.before = 0x1122334455667788ULL;
  frame.after = 0x8877665544332211ULL;
  atomic_init(&frame.value, CMPLX(0.0, 0.0));
  frame.value = callback();
  snapshot = atomic_load(&frame.value);

  assert(frame.before == 0x1122334455667788ULL);
  assert(frame.after == 0x8877665544332211ULL);
  assert(creal(snapshot) == 11.5);
  assert(cimag(snapshot) == 30.5);
  return (int)(creal(snapshot) + cimag(snapshot));
}

int main(void) {
  plain_float_consumer_function *plain_float_consumer =
      consume_plain_float;
  atomic_float_consumer_function *atomic_float_consumer =
      consume_atomic_float;
  plain_double_consumer_function *plain_double_consumer =
      consume_plain_double;
  atomic_double_consumer_function *atomic_double_consumer =
      consume_atomic_double;

  _Static_assert(
      CLASSIFY_CONSUMER(&consume_plain_float) == 1,
      "plain float complex callback remains non-atomic");
  _Static_assert(
      CLASSIFY_CONSUMER(&consume_atomic_float) == 2,
      "atomic float complex callback remains distinct");
  _Static_assert(
      CLASSIFY_CONSUMER(&consume_plain_double) == 3,
      "plain double complex callback remains non-atomic");
  _Static_assert(
      CLASSIFY_CONSUMER(&consume_atomic_double) == 4,
      "atomic double complex callback remains distinct");

  assert(plain_float_consumer(plain_float_callback) == 42);
  assert(atomic_float_consumer(atomic_float_callback) == 42);
  assert(plain_double_consumer(plain_double_callback) == 42);
  assert(atomic_double_consumer(atomic_double_callback) == 42);
  return 0;
}
