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

struct guarded_atomic_long {
  unsigned long before;
  _Atomic(long) value;
  unsigned long after;
};

struct guarded_atomic_unsigned_long {
  unsigned long before;
  _Atomic(unsigned long) value;
  unsigned long after;
};

struct guarded_atomic_long_long {
  unsigned long long before;
  _Atomic(long long) value;
  unsigned long long after;
};

struct guarded_atomic_unsigned_long_long {
  unsigned long long before;
  _Atomic(unsigned long long) value;
  unsigned long long after;
};

typedef int plain_long_callback_function(long value);
typedef int atomic_long_callback_function(
    _Atomic(long) value);
typedef int plain_unsigned_long_callback_function(
    unsigned long value);
typedef int atomic_unsigned_long_callback_function(
    _Atomic(unsigned long) value);
typedef int plain_long_long_callback_function(
    long long value);
typedef int atomic_long_long_callback_function(
    _Atomic(long long) value);
typedef int plain_unsigned_long_long_callback_function(
    unsigned long long value);
typedef int atomic_unsigned_long_long_callback_function(
    _Atomic(unsigned long long) value);

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

static int inspect_plain_long(long value) {
  assert(value == -2000000000L);
  return 42;
}

static int inspect_atomic_long(_Atomic(long) value) {
  long snapshot = atomic_load(&value);

  atomic_store(&value, 17L);
  assert(atomic_load(&value) == 17L);
  assert(snapshot == -2000000000L);
  return 42;
}

static int inspect_plain_unsigned_long(unsigned long value) {
  assert(value == 4000000000UL);
  return 42;
}

static int inspect_atomic_unsigned_long(
    _Atomic(unsigned long) value) {
  unsigned long snapshot = atomic_load(&value);

  atomic_store(&value, 19UL);
  assert(atomic_load(&value) == 19UL);
  assert(snapshot == 4000000000UL);
  return 42;
}

static int inspect_plain_long_long(long long value) {
  assert(value == -5000000000LL);
  return 42;
}

static int inspect_atomic_long_long(
    _Atomic(long long) value) {
  long long snapshot = atomic_load(&value);

  atomic_store(&value, 23LL);
  assert(atomic_load(&value) == 23LL);
  assert(snapshot == -5000000000LL);
  return 42;
}

static int inspect_plain_unsigned_long_long(
    unsigned long long value) {
  assert(value == 0xfedcba9876543210ULL);
  return 42;
}

static int inspect_atomic_unsigned_long_long(
    _Atomic(unsigned long long) value) {
  unsigned long long snapshot = atomic_load(&value);

  atomic_store(&value, 29ULL);
  assert(atomic_load(&value) == 29ULL);
  assert(snapshot == 0xfedcba9876543210ULL);
  return 42;
}

static int consume_plain_long(
    plain_long_callback_function *callback) {
  return callback(-2000000000L);
}

static int consume_atomic_long(
    atomic_long_callback_function *callback) {
  struct guarded_atomic_long frame;
  long snapshot;
  int result;

  frame.before = 0x11223344UL;
  frame.after = 0x55667788UL;
  atomic_init(&frame.value, -2000000000L);
  result = callback(frame.value);
  snapshot = atomic_load(&frame.value);

  assert(frame.before == 0x11223344UL);
  assert(frame.after == 0x55667788UL);
  assert(snapshot == -2000000000L);
  return result;
}

static int consume_plain_unsigned_long(
    plain_unsigned_long_callback_function *callback) {
  return callback(4000000000UL);
}

static int consume_atomic_unsigned_long(
    atomic_unsigned_long_callback_function *callback) {
  struct guarded_atomic_unsigned_long frame;
  unsigned long snapshot;
  int result;

  frame.before = 0x10203040UL;
  frame.after = 0x50607080UL;
  atomic_init(&frame.value, 4000000000UL);
  result = callback(frame.value);
  snapshot = atomic_load(&frame.value);

  assert(frame.before == 0x10203040UL);
  assert(frame.after == 0x50607080UL);
  assert(snapshot == 4000000000UL);
  return result;
}

static int consume_plain_long_long(
    plain_long_long_callback_function *callback) {
  return callback(-5000000000LL);
}

static int consume_atomic_long_long(
    atomic_long_long_callback_function *callback) {
  struct guarded_atomic_long_long frame;
  long long snapshot;
  int result;

  frame.before = 0x1122334455667788ULL;
  frame.after = 0x8877665544332211ULL;
  atomic_init(&frame.value, -5000000000LL);
  result = callback(frame.value);
  snapshot = atomic_load(&frame.value);

  assert(frame.before == 0x1122334455667788ULL);
  assert(frame.after == 0x8877665544332211ULL);
  assert(snapshot == -5000000000LL);
  return result;
}

static int consume_plain_unsigned_long_long(
    plain_unsigned_long_long_callback_function *callback) {
  return callback(0xfedcba9876543210ULL);
}

static int consume_atomic_unsigned_long_long(
    atomic_unsigned_long_long_callback_function *callback) {
  struct guarded_atomic_unsigned_long_long frame;
  unsigned long long snapshot;
  int result;

  frame.before = 0x1020304050607080ULL;
  frame.after = 0x8070605040302010ULL;
  atomic_init(&frame.value, 0xfedcba9876543210ULL);
  result = callback(frame.value);
  snapshot = atomic_load(&frame.value);

  assert(frame.before == 0x1020304050607080ULL);
  assert(frame.after == 0x8070605040302010ULL);
  assert(snapshot == 0xfedcba9876543210ULL);
  return result;
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
      "plain long callback parameter remains non-atomic");
  _Static_assert(
      CLASSIFY_CONSUMER(&consume_atomic_long) == 2,
      "atomic long callback parameter remains distinct");
  _Static_assert(
      CLASSIFY_CONSUMER(&consume_plain_unsigned_long) == 3,
      "plain unsigned long callback parameter remains non-atomic");
  _Static_assert(
      CLASSIFY_CONSUMER(&consume_atomic_unsigned_long) == 4,
      "atomic unsigned long callback parameter remains distinct");
  _Static_assert(
      CLASSIFY_CONSUMER(&consume_plain_long_long) == 5,
      "plain long long callback parameter remains non-atomic");
  _Static_assert(
      CLASSIFY_CONSUMER(&consume_atomic_long_long) == 6,
      "atomic long long callback parameter remains distinct");
  _Static_assert(
      CLASSIFY_CONSUMER(&consume_plain_unsigned_long_long) == 7,
      "plain unsigned long long callback parameter remains non-atomic");
  _Static_assert(
      CLASSIFY_CONSUMER(&consume_atomic_unsigned_long_long) == 8,
      "atomic unsigned long long callback parameter remains distinct");

  assert(plain_long_consumer(inspect_plain_long) == 42);
  assert(atomic_long_consumer(inspect_atomic_long) == 42);
  assert(plain_unsigned_long_consumer(
             inspect_plain_unsigned_long) == 42);
  assert(atomic_unsigned_long_consumer(
             inspect_atomic_unsigned_long) == 42);
  assert(plain_long_long_consumer(
             inspect_plain_long_long) == 42);
  assert(atomic_long_long_consumer(
             inspect_atomic_long_long) == 42);
  assert(plain_unsigned_long_long_consumer(
             inspect_plain_unsigned_long_long) == 42);
  assert(atomic_unsigned_long_long_consumer(
             inspect_atomic_unsigned_long_long) == 42);
  return 0;
}
