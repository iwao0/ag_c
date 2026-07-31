#include <assert.h>
#include <stdatomic.h>

#define CLASSIFY_CONSUMER(expression) \
  _Generic((expression), \
           plain_signed_consumer_function *: 1, \
           atomic_signed_consumer_function *: 2, \
           plain_unsigned_consumer_function *: 3, \
           atomic_unsigned_consumer_function *: 4, \
           default: 0)

enum signed_state {
  SIGNED_STATE_NEGATIVE = -1,
  SIGNED_STATE_VALUE = 17
};

enum unsigned_state {
  UNSIGNED_STATE_ZERO = 0,
  UNSIGNED_STATE_VALUE = 25
};

struct guarded_atomic_signed_state {
  unsigned int before;
  _Atomic(enum signed_state) value;
  unsigned int after;
};

struct guarded_atomic_unsigned_state {
  unsigned int before;
  _Atomic(enum unsigned_state) value;
  unsigned int after;
};

typedef int plain_signed_callback_function(
    enum signed_state value);
typedef int atomic_signed_callback_function(
    _Atomic(enum signed_state) value);
typedef int atomic_int_callback_function(
    _Atomic(int) value);

typedef int plain_unsigned_callback_function(
    enum unsigned_state value);
typedef int atomic_unsigned_callback_function(
    _Atomic(enum unsigned_state) value);
typedef int atomic_unsigned_int_callback_function(
    _Atomic(unsigned int) value);

typedef int plain_signed_consumer_function(
    plain_signed_callback_function *callback);
typedef int atomic_signed_consumer_function(
    atomic_signed_callback_function *callback);
typedef int plain_unsigned_consumer_function(
    plain_unsigned_callback_function *callback);
typedef int atomic_unsigned_consumer_function(
    atomic_unsigned_callback_function *callback);

static int inspect_plain_signed(enum signed_state value) {
  return value;
}

static int inspect_atomic_signed(
    _Atomic(enum signed_state) value) {
  enum signed_state snapshot = atomic_load(&value);

  atomic_store(&value, SIGNED_STATE_NEGATIVE);
  assert(atomic_load(&value) == SIGNED_STATE_NEGATIVE);
  return snapshot;
}

static int inspect_atomic_int(_Atomic(int) value) {
  int snapshot = atomic_load(&value);

  atomic_store(&value, -1);
  assert(atomic_load(&value) == -1);
  return snapshot;
}

static int inspect_plain_unsigned(enum unsigned_state value) {
  return (int)value;
}

static int inspect_atomic_unsigned(
    _Atomic(enum unsigned_state) value) {
  enum unsigned_state snapshot = atomic_load(&value);

  atomic_store(&value, UNSIGNED_STATE_ZERO);
  assert(atomic_load(&value) == UNSIGNED_STATE_ZERO);
  return (int)snapshot;
}

static int inspect_atomic_unsigned_int(
    _Atomic(unsigned int) value) {
  unsigned int snapshot = atomic_load(&value);

  atomic_store(&value, 0u);
  assert(atomic_load(&value) == 0u);
  return (int)snapshot;
}

static int consume_plain_signed(
    plain_signed_callback_function *callback) {
  return callback(SIGNED_STATE_VALUE);
}

static int consume_atomic_signed(
    atomic_signed_callback_function *callback) {
  struct guarded_atomic_signed_state frame;
  enum signed_state snapshot;
  int result;

  frame.before = 0x11223344u;
  frame.after = 0x55667788u;
  atomic_init(&frame.value, SIGNED_STATE_VALUE);
  result = callback(frame.value);
  snapshot = atomic_load(&frame.value);

  assert(frame.before == 0x11223344u);
  assert(frame.after == 0x55667788u);
  assert(snapshot == SIGNED_STATE_VALUE);
  return result;
}

static int consume_plain_unsigned(
    plain_unsigned_callback_function *callback) {
  return callback(UNSIGNED_STATE_VALUE);
}

static int consume_atomic_unsigned(
    atomic_unsigned_callback_function *callback) {
  struct guarded_atomic_unsigned_state frame;
  enum unsigned_state snapshot;
  int result;

  frame.before = 0x88776655u;
  frame.after = 0x44332211u;
  atomic_init(&frame.value, UNSIGNED_STATE_VALUE);
  result = callback(frame.value);
  snapshot = atomic_load(&frame.value);

  assert(frame.before == 0x88776655u);
  assert(frame.after == 0x44332211u);
  assert(snapshot == UNSIGNED_STATE_VALUE);
  return result;
}

int main(void) {
  plain_signed_consumer_function *plain_signed_consumer =
      consume_plain_signed;
  atomic_signed_consumer_function *atomic_signed_consumer =
      consume_atomic_signed;
  plain_unsigned_consumer_function *plain_unsigned_consumer =
      consume_plain_unsigned;
  atomic_unsigned_consumer_function *atomic_unsigned_consumer =
      consume_atomic_unsigned;
  atomic_signed_callback_function *compatible_signed_callback =
      inspect_atomic_int;
  atomic_unsigned_callback_function *compatible_unsigned_callback =
      inspect_atomic_unsigned_int;

  _Static_assert(
      _Generic((enum signed_state *)0,
               int *: 1,
               default: 0),
      "negative enum uses signed int compatibility");
  _Static_assert(
      _Generic((enum unsigned_state *)0,
               unsigned int *: 1,
               default: 0),
      "nonnegative enum uses unsigned int compatibility");
  _Static_assert(
      CLASSIFY_CONSUMER(&consume_plain_signed) == 1,
      "plain signed enum callback parameter remains non-atomic");
  _Static_assert(
      CLASSIFY_CONSUMER(&consume_atomic_signed) == 2,
      "atomic signed enum callback parameter remains distinct");
  _Static_assert(
      CLASSIFY_CONSUMER(&consume_plain_unsigned) == 3,
      "plain unsigned enum callback parameter remains non-atomic");
  _Static_assert(
      CLASSIFY_CONSUMER(&consume_atomic_unsigned) == 4,
      "atomic unsigned enum callback parameter remains distinct");

  assert(plain_signed_consumer(inspect_plain_signed) == 17);
  assert(atomic_signed_consumer(inspect_atomic_signed) == 17);
  assert(atomic_signed_consumer(compatible_signed_callback) == 17);
  assert(plain_unsigned_consumer(inspect_plain_unsigned) == 25);
  assert(atomic_unsigned_consumer(inspect_atomic_unsigned) == 25);
  assert(atomic_unsigned_consumer(compatible_unsigned_callback) == 25);
  return 0;
}
