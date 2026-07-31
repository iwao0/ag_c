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

struct guarded_atomic_float {
  unsigned int before;
  _Atomic(float) value;
  unsigned int after;
};

struct guarded_atomic_double {
  unsigned long long before;
  _Atomic(double) value;
  unsigned long long after;
};

struct guarded_atomic_long_double {
  unsigned long long before;
  _Atomic(long double) value;
  unsigned long long after;
};

typedef int plain_float_callback_function(float value);
typedef int atomic_float_callback_function(
    _Atomic(float) value);
typedef int plain_double_callback_function(double value);
typedef int atomic_double_callback_function(
    _Atomic(double) value);
typedef int plain_long_double_callback_function(
    long double value);
typedef int atomic_long_double_callback_function(
    _Atomic(long double) value);

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

static int inspect_plain_float(float value) {
  assert(value == 17.5f);
  return 42;
}

static int inspect_atomic_float(_Atomic(float) value) {
  float snapshot = atomic_load(&value);

  atomic_store(&value, 1.25f);
  assert(atomic_load(&value) == 1.25f);
  assert(snapshot == 17.5f);
  return 42;
}

static int inspect_plain_double(double value) {
  assert(value == 25.25);
  return 42;
}

static int inspect_atomic_double(_Atomic(double) value) {
  double snapshot = atomic_load(&value);

  atomic_store(&value, 2.5);
  assert(atomic_load(&value) == 2.5);
  assert(snapshot == 25.25);
  return 42;
}

static int inspect_plain_long_double(long double value) {
  assert(value == 31.75L);
  return 42;
}

static int inspect_atomic_long_double(
    _Atomic(long double) value) {
  long double snapshot = atomic_load(&value);

  atomic_store(&value, 3.75L);
  assert(atomic_load(&value) == 3.75L);
  assert(snapshot == 31.75L);
  return 42;
}

static int consume_plain_float(
    plain_float_callback_function *callback) {
  return callback(17.5f);
}

static int consume_atomic_float(
    atomic_float_callback_function *callback) {
  struct guarded_atomic_float frame;
  float snapshot;
  int result;

  frame.before = 0x11223344u;
  frame.after = 0x55667788u;
  atomic_init(&frame.value, 17.5f);
  result = callback(frame.value);
  snapshot = atomic_load(&frame.value);

  assert(frame.before == 0x11223344u);
  assert(frame.after == 0x55667788u);
  assert(snapshot == 17.5f);
  return result;
}

static int consume_plain_double(
    plain_double_callback_function *callback) {
  return callback(25.25);
}

static int consume_atomic_double(
    atomic_double_callback_function *callback) {
  struct guarded_atomic_double frame;
  double snapshot;
  int result;

  frame.before = 0x1122334455667788ULL;
  frame.after = 0x8877665544332211ULL;
  atomic_init(&frame.value, 25.25);
  result = callback(frame.value);
  snapshot = atomic_load(&frame.value);

  assert(frame.before == 0x1122334455667788ULL);
  assert(frame.after == 0x8877665544332211ULL);
  assert(snapshot == 25.25);
  return result;
}

static int consume_plain_long_double(
    plain_long_double_callback_function *callback) {
  return callback(31.75L);
}

static int consume_atomic_long_double(
    atomic_long_double_callback_function *callback) {
  struct guarded_atomic_long_double frame;
  long double snapshot;
  int result;

  frame.before = 0x1020304050607080ULL;
  frame.after = 0x8070605040302010ULL;
  atomic_init(&frame.value, 31.75L);
  result = callback(frame.value);
  snapshot = atomic_load(&frame.value);

  assert(frame.before == 0x1020304050607080ULL);
  assert(frame.after == 0x8070605040302010ULL);
  assert(snapshot == 31.75L);
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
  plain_long_double_consumer_function *plain_long_double_consumer =
      consume_plain_long_double;
  atomic_long_double_consumer_function *atomic_long_double_consumer =
      consume_atomic_long_double;

  _Static_assert(
      CLASSIFY_CONSUMER(&consume_plain_float) == 1,
      "plain float callback parameter remains non-atomic");
  _Static_assert(
      CLASSIFY_CONSUMER(&consume_atomic_float) == 2,
      "atomic float callback parameter remains distinct");
  _Static_assert(
      CLASSIFY_CONSUMER(&consume_plain_double) == 3,
      "plain double callback parameter remains non-atomic");
  _Static_assert(
      CLASSIFY_CONSUMER(&consume_atomic_double) == 4,
      "atomic double callback parameter remains distinct");
  _Static_assert(
      CLASSIFY_CONSUMER(&consume_plain_long_double) == 5,
      "plain long double callback parameter remains non-atomic");
  _Static_assert(
      CLASSIFY_CONSUMER(&consume_atomic_long_double) == 6,
      "atomic long double callback parameter remains distinct");

  assert(plain_float_consumer(inspect_plain_float) == 42);
  assert(atomic_float_consumer(inspect_atomic_float) == 42);
  assert(plain_double_consumer(inspect_plain_double) == 42);
  assert(atomic_double_consumer(inspect_atomic_double) == 42);
  assert(plain_long_double_consumer(
             inspect_plain_long_double) == 42);
  assert(atomic_long_double_consumer(
             inspect_atomic_long_double) == 42);
  return 0;
}
