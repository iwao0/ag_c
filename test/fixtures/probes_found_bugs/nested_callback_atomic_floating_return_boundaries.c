#include <assert.h>
#include <stdatomic.h>

#define CLASSIFY_CONSUMER(expression) \
  _Generic((expression), \
           plain_float_consumer_function *: 1, \
           atomic_float_consumer_function *: 2, \
           plain_double_consumer_function *: 3, \
           atomic_double_consumer_function *: 4, \
           plain_long_double_consumer_function *: 5, \
           atomic_long_double_consumer_function *: 6, \
           default: 0)

struct guarded_float {
  unsigned int before;
  _Atomic(float) value;
  unsigned int after;
};

struct guarded_double {
  unsigned long long before;
  _Atomic(double) value;
  unsigned long long after;
};

struct guarded_long_double {
  unsigned long long before;
  _Atomic(long double) value;
  unsigned long long after;
};

typedef float plain_float_callback_function(void);
typedef _Atomic(float) atomic_float_callback_function(void);
typedef double plain_double_callback_function(void);
typedef _Atomic(double) atomic_double_callback_function(void);
typedef long double plain_long_double_callback_function(void);
typedef _Atomic(long double)
    atomic_long_double_callback_function(void);

typedef int plain_float_consumer_function(
    plain_float_callback_function *callback);
typedef int atomic_float_consumer_function(
    atomic_float_callback_function *callback);
typedef int plain_double_consumer_function(
    plain_double_callback_function *callback);
typedef int atomic_double_consumer_function(
    atomic_double_callback_function *callback);
typedef int plain_long_double_consumer_function(
    plain_long_double_callback_function *callback);
typedef int atomic_long_double_consumer_function(
    atomic_long_double_callback_function *callback);

static float plain_float_callback(void) {
  return 17.5f;
}

static _Atomic(float) atomic_float_callback(void) {
  return 17.5f;
}

static double plain_double_callback(void) {
  return 25.25;
}

static _Atomic(double) atomic_double_callback(void) {
  return 25.25;
}

static long double plain_long_double_callback(void) {
  return 31.75L;
}

static _Atomic(long double)
atomic_long_double_callback(void) {
  return 31.75L;
}

static int consume_plain_float(
    plain_float_callback_function *callback) {
  assert(callback() == 17.5f);
  return 42;
}

static int consume_atomic_float(
    atomic_float_callback_function *callback) {
  struct guarded_float frame;
  float snapshot;

  frame.before = 0x11223344u;
  frame.after = 0x55667788u;
  atomic_init(&frame.value, 0.0f);
  frame.value = callback();
  snapshot = atomic_load(&frame.value);

  assert(frame.before == 0x11223344u);
  assert(frame.after == 0x55667788u);
  assert(snapshot == 17.5f);
  return 42;
}

static int consume_plain_double(
    plain_double_callback_function *callback) {
  assert(callback() == 25.25);
  return 42;
}

static int consume_atomic_double(
    atomic_double_callback_function *callback) {
  struct guarded_double frame;
  double snapshot;

  frame.before = 0x1122334455667788ULL;
  frame.after = 0x8877665544332211ULL;
  atomic_init(&frame.value, 0.0);
  frame.value = callback();
  snapshot = atomic_load(&frame.value);

  assert(frame.before == 0x1122334455667788ULL);
  assert(frame.after == 0x8877665544332211ULL);
  assert(snapshot == 25.25);
  return 42;
}

static int consume_plain_long_double(
    plain_long_double_callback_function *callback) {
  assert(callback() == 31.75L);
  return 42;
}

static int consume_atomic_long_double(
    atomic_long_double_callback_function *callback) {
  struct guarded_long_double frame;
  long double snapshot;

  frame.before = 0x1122334455667788ULL;
  frame.after = 0x8877665544332211ULL;
  atomic_init(&frame.value, 0.0L);
  frame.value = callback();
  snapshot = atomic_load(&frame.value);

  assert(frame.before == 0x1122334455667788ULL);
  assert(frame.after == 0x8877665544332211ULL);
  assert(snapshot == 31.75L);
  return 42;
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
  plain_long_double_consumer_function *plain_long_double_consumer =
      consume_plain_long_double;
  atomic_long_double_consumer_function *atomic_long_double_consumer =
      consume_atomic_long_double;

  _Static_assert(
      CLASSIFY_CONSUMER(&consume_plain_float) == 1,
      "plain float callback remains non-atomic");
  _Static_assert(
      CLASSIFY_CONSUMER(&consume_atomic_float) == 2,
      "atomic float callback remains distinct");
  _Static_assert(
      CLASSIFY_CONSUMER(&consume_plain_double) == 3,
      "plain double callback remains non-atomic");
  _Static_assert(
      CLASSIFY_CONSUMER(&consume_atomic_double) == 4,
      "atomic double callback remains distinct");
  _Static_assert(
      CLASSIFY_CONSUMER(&consume_plain_long_double) == 5,
      "plain long double callback remains non-atomic");
  _Static_assert(
      CLASSIFY_CONSUMER(&consume_atomic_long_double) == 6,
      "atomic long double callback remains distinct");

  assert(plain_float_consumer(plain_float_callback) == 42);
  assert(atomic_float_consumer(atomic_float_callback) == 42);
  assert(plain_double_consumer(plain_double_callback) == 42);
  assert(atomic_double_consumer(atomic_double_callback) == 42);
  assert(plain_long_double_consumer(
             plain_long_double_callback) == 42);
  assert(atomic_long_double_consumer(
             atomic_long_double_callback) == 42);
  return 0;
}
