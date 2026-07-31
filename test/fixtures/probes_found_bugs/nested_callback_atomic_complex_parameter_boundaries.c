#include <assert.h>
#include <complex.h>
#include <stdint.h>
#include <stdatomic.h>

#define CLASSIFY_CONSUMER(expression) \
  _Generic((expression), \
           plain_float_consumer_function *: 1, \
           atomic_float_consumer_function *: 2, \
           plain_double_consumer_function *: 3, \
           atomic_double_consumer_function *: 4, \
           default: 0)

struct guarded_atomic_float_complex {
  unsigned int before;
  _Atomic(float complex) value;
  unsigned int after;
};

struct guarded_atomic_double_complex {
  unsigned long long before;
  _Atomic(double complex) value;
  unsigned long long after;
};

typedef int plain_float_callback_function(float complex value);
typedef int atomic_float_callback_function(
    _Atomic(float complex) value);
typedef int plain_double_callback_function(double complex value);
typedef int atomic_double_callback_function(
    _Atomic(double complex) value);

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
_Static_assert(_Alignof(_Atomic(float complex)) == 8,
               "atomic float complex storage alignment");
_Static_assert(sizeof(_Atomic(double complex)) == 16,
               "atomic double complex storage size");

#ifdef __wasm32__
_Static_assert(_Alignof(_Atomic(double complex)) == 8,
               "wasm32 atomic double complex alignment");
#else
_Static_assert(_Alignof(_Atomic(double complex)) == 16,
               "native atomic double complex alignment");
#endif

static int sum_float_complex(float complex value) {
  return (int)(crealf(value) + cimagf(value));
}

static int sum_double_complex(double complex value) {
  return (int)(creal(value) + cimag(value));
}

static int inspect_plain_float(float complex value) {
  return sum_float_complex(value);
}

static int inspect_atomic_float(
    _Atomic(float complex) value) {
  float complex snapshot = atomic_load(&value);
  float complex replacement = CMPLXF(1.5f, 4.5f);

  assert((uintptr_t)&value %
             _Alignof(_Atomic(float complex)) ==
         0);
  atomic_store(&value, replacement);
  assert(sum_float_complex(atomic_load(&value)) == 6);
  return sum_float_complex(snapshot);
}

static int inspect_plain_double(double complex value) {
  return sum_double_complex(value);
}

static int inspect_atomic_double(
    _Atomic(double complex) value) {
  double complex snapshot = atomic_load(&value);
  double complex replacement = CMPLX(2.25, 3.75);

  assert((uintptr_t)&value %
             _Alignof(_Atomic(double complex)) ==
         0);
  atomic_store(&value, replacement);
  assert(sum_double_complex(atomic_load(&value)) == 6);
  return sum_double_complex(snapshot);
}

static int consume_plain_float(
    plain_float_callback_function *callback) {
  return callback(CMPLXF(19.5f, 22.5f));
}

static int consume_atomic_float(
    atomic_float_callback_function *callback) {
  struct guarded_atomic_float_complex frame;
  float complex snapshot;
  int result;

  frame.before = 0x11223344u;
  frame.after = 0x55667788u;
  atomic_init(&frame.value, CMPLXF(17.5f, 24.5f));
  result = callback(frame.value);
  snapshot = atomic_load(&frame.value);

  assert(frame.before == 0x11223344u);
  assert(frame.after == 0x55667788u);
  assert(sum_float_complex(snapshot) == 42);
  return result;
}

static int consume_plain_double(
    plain_double_callback_function *callback) {
  return callback(CMPLX(13.25, 28.75));
}

static int consume_atomic_double(
    atomic_double_callback_function *callback) {
  struct guarded_atomic_double_complex frame;
  double complex snapshot;
  int result;

  frame.before = 0x1122334455667788ULL;
  frame.after = 0x8877665544332211ULL;
  atomic_init(&frame.value, CMPLX(11.5, 30.5));
  result = callback(frame.value);
  snapshot = atomic_load(&frame.value);

  assert(frame.before == 0x1122334455667788ULL);
  assert(frame.after == 0x8877665544332211ULL);
  assert(sum_double_complex(snapshot) == 42);
  return result;
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
      "plain float complex callback parameter remains non-atomic");
  _Static_assert(
      CLASSIFY_CONSUMER(&consume_atomic_float) == 2,
      "atomic float complex callback parameter remains distinct");
  _Static_assert(
      CLASSIFY_CONSUMER(&consume_plain_double) == 3,
      "plain double complex callback parameter remains non-atomic");
  _Static_assert(
      CLASSIFY_CONSUMER(&consume_atomic_double) == 4,
      "atomic double complex callback parameter remains distinct");

  assert(plain_float_consumer(inspect_plain_float) == 42);
  assert(atomic_float_consumer(inspect_atomic_float) == 42);
  assert(plain_double_consumer(inspect_plain_double) == 42);
  assert(atomic_double_consumer(inspect_atomic_double) == 42);
  return 0;
}
