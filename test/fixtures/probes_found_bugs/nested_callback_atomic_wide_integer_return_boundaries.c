#include <assert.h>
#include <stdatomic.h>

#define CLASSIFY_CONSUMER(expression) \
  _Generic((expression), \
           plain_long_consumer_function *: 1, \
           atomic_long_consumer_function *: 2, \
           plain_unsigned_long_consumer_function *: 3, \
           atomic_unsigned_long_consumer_function *: 4, \
           plain_long_long_consumer_function *: 5, \
           atomic_long_long_consumer_function *: 6, \
           plain_unsigned_long_long_consumer_function *: 7, \
           atomic_unsigned_long_long_consumer_function *: 8, \
           default: 0)

struct guarded_long {
  unsigned long before;
  _Atomic(long) value;
  unsigned long after;
};

struct guarded_unsigned_long {
  unsigned long before;
  _Atomic(unsigned long) value;
  unsigned long after;
};

struct guarded_long_long {
  unsigned long long before;
  _Atomic(long long) value;
  unsigned long long after;
};

struct guarded_unsigned_long_long {
  unsigned long long before;
  _Atomic(unsigned long long) value;
  unsigned long long after;
};

typedef long plain_long_callback_function(void);
typedef _Atomic(long) atomic_long_callback_function(void);
typedef unsigned long plain_unsigned_long_callback_function(void);
typedef _Atomic(unsigned long)
    atomic_unsigned_long_callback_function(void);
typedef long long plain_long_long_callback_function(void);
typedef _Atomic(long long)
    atomic_long_long_callback_function(void);
typedef unsigned long long
    plain_unsigned_long_long_callback_function(void);
typedef _Atomic(unsigned long long)
    atomic_unsigned_long_long_callback_function(void);

typedef int plain_long_consumer_function(
    plain_long_callback_function *callback);
typedef int atomic_long_consumer_function(
    atomic_long_callback_function *callback);
typedef int plain_unsigned_long_consumer_function(
    plain_unsigned_long_callback_function *callback);
typedef int atomic_unsigned_long_consumer_function(
    atomic_unsigned_long_callback_function *callback);
typedef int plain_long_long_consumer_function(
    plain_long_long_callback_function *callback);
typedef int atomic_long_long_consumer_function(
    atomic_long_long_callback_function *callback);
typedef int plain_unsigned_long_long_consumer_function(
    plain_unsigned_long_long_callback_function *callback);
typedef int atomic_unsigned_long_long_consumer_function(
    atomic_unsigned_long_long_callback_function *callback);

_Static_assert(sizeof(_Atomic(long)) == 8,
               "atomic long storage size");
_Static_assert(sizeof(_Atomic(unsigned long)) == 8,
               "atomic unsigned long storage size");

_Static_assert(sizeof(_Atomic(long long)) == 8,
               "atomic long long storage size");
_Static_assert(sizeof(_Atomic(unsigned long long)) == 8,
               "atomic unsigned long long storage size");

static long plain_long_callback(void) {
  return -2000000000L;
}

static _Atomic(long) atomic_long_callback(void) {
  return -2000000000L;
}

static unsigned long plain_unsigned_long_callback(void) {
  return 4000000000UL;
}

static _Atomic(unsigned long)
atomic_unsigned_long_callback(void) {
  return 4000000000UL;
}

static long long plain_long_long_callback(void) {
  return -5000000000LL;
}

static _Atomic(long long)
atomic_long_long_callback(void) {
  return -5000000000LL;
}

static unsigned long long
plain_unsigned_long_long_callback(void) {
  return 0xfedcba9876543210ULL;
}

static _Atomic(unsigned long long)
atomic_unsigned_long_long_callback(void) {
  return 0xfedcba9876543210ULL;
}

static int consume_plain_long(
    plain_long_callback_function *callback) {
  assert(callback() == -2000000000L);
  return 42;
}

static int consume_atomic_long(
    atomic_long_callback_function *callback) {
  struct guarded_long frame;
  long snapshot;

  frame.before = 0x11223344UL;
  frame.after = 0x55667788UL;
  atomic_init(&frame.value, 0L);
  frame.value = callback();
  snapshot = atomic_load(&frame.value);

  assert(frame.before == 0x11223344UL);
  assert(frame.after == 0x55667788UL);
  assert(snapshot == -2000000000L);
  return 42;
}

static int consume_plain_unsigned_long(
    plain_unsigned_long_callback_function *callback) {
  assert(callback() == 4000000000UL);
  return 42;
}

static int consume_atomic_unsigned_long(
    atomic_unsigned_long_callback_function *callback) {
  struct guarded_unsigned_long frame;
  unsigned long snapshot;

  frame.before = 0x11223344UL;
  frame.after = 0x55667788UL;
  atomic_init(&frame.value, 0UL);
  frame.value = callback();
  snapshot = atomic_load(&frame.value);

  assert(frame.before == 0x11223344UL);
  assert(frame.after == 0x55667788UL);
  assert(snapshot == 4000000000UL);
  return 42;
}

static int consume_plain_long_long(
    plain_long_long_callback_function *callback) {
  assert(callback() == -5000000000LL);
  return 42;
}

static int consume_atomic_long_long(
    atomic_long_long_callback_function *callback) {
  struct guarded_long_long frame;
  long long snapshot;

  frame.before = 0x1122334455667788ULL;
  frame.after = 0x8877665544332211ULL;
  atomic_init(&frame.value, 0LL);
  frame.value = callback();
  snapshot = atomic_load(&frame.value);

  assert(frame.before == 0x1122334455667788ULL);
  assert(frame.after == 0x8877665544332211ULL);
  assert(snapshot == -5000000000LL);
  return 42;
}

static int consume_plain_unsigned_long_long(
    plain_unsigned_long_long_callback_function *callback) {
  assert(callback() == 0xfedcba9876543210ULL);
  return 42;
}

static int consume_atomic_unsigned_long_long(
    atomic_unsigned_long_long_callback_function *callback) {
  struct guarded_unsigned_long_long frame;
  unsigned long long snapshot;

  frame.before = 0x1122334455667788ULL;
  frame.after = 0x8877665544332211ULL;
  atomic_init(&frame.value, 0ULL);
  frame.value = callback();
  snapshot = atomic_load(&frame.value);

  assert(frame.before == 0x1122334455667788ULL);
  assert(frame.after == 0x8877665544332211ULL);
  assert(snapshot == 0xfedcba9876543210ULL);
  return 42;
}

int main(void) {
  plain_long_consumer_function *plain_long_consumer =
      consume_plain_long;
  atomic_long_consumer_function *atomic_long_consumer =
      consume_atomic_long;
  plain_unsigned_long_consumer_function *plain_unsigned_long_consumer =
      consume_plain_unsigned_long;
  atomic_unsigned_long_consumer_function *atomic_unsigned_long_consumer =
      consume_atomic_unsigned_long;
  plain_long_long_consumer_function *plain_long_long_consumer =
      consume_plain_long_long;
  atomic_long_long_consumer_function *atomic_long_long_consumer =
      consume_atomic_long_long;
  plain_unsigned_long_long_consumer_function
      *plain_unsigned_long_long_consumer =
          consume_plain_unsigned_long_long;
  atomic_unsigned_long_long_consumer_function
      *atomic_unsigned_long_long_consumer =
          consume_atomic_unsigned_long_long;

  _Static_assert(
      CLASSIFY_CONSUMER(&consume_plain_long) == 1,
      "plain long callback remains non-atomic");
  _Static_assert(
      CLASSIFY_CONSUMER(&consume_atomic_long) == 2,
      "atomic long callback remains distinct");
  _Static_assert(
      CLASSIFY_CONSUMER(&consume_plain_unsigned_long) == 3,
      "plain unsigned long callback remains non-atomic");
  _Static_assert(
      CLASSIFY_CONSUMER(&consume_atomic_unsigned_long) == 4,
      "atomic unsigned long callback remains distinct");
  _Static_assert(
      CLASSIFY_CONSUMER(&consume_plain_long_long) == 5,
      "plain long long callback remains non-atomic");
  _Static_assert(
      CLASSIFY_CONSUMER(&consume_atomic_long_long) == 6,
      "atomic long long callback remains distinct");
  _Static_assert(
      CLASSIFY_CONSUMER(&consume_plain_unsigned_long_long) == 7,
      "plain unsigned long long callback remains non-atomic");
  _Static_assert(
      CLASSIFY_CONSUMER(&consume_atomic_unsigned_long_long) == 8,
      "atomic unsigned long long callback remains distinct");

  assert(plain_long_consumer(plain_long_callback) == 42);
  assert(atomic_long_consumer(atomic_long_callback) == 42);
  assert(plain_unsigned_long_consumer(
             plain_unsigned_long_callback) == 42);
  assert(atomic_unsigned_long_consumer(
             atomic_unsigned_long_callback) == 42);
  assert(plain_long_long_consumer(
             plain_long_long_callback) == 42);
  assert(atomic_long_long_consumer(
             atomic_long_long_callback) == 42);
  assert(plain_unsigned_long_long_consumer(
             plain_unsigned_long_long_callback) == 42);
  assert(atomic_unsigned_long_long_consumer(
             atomic_unsigned_long_long_callback) == 42);
  return 0;
}
