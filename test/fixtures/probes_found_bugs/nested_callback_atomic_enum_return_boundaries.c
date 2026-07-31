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

typedef enum signed_state plain_signed_callback_function(void);
typedef _Atomic(enum signed_state)
    atomic_signed_callback_function(void);
typedef _Atomic(int) atomic_int_callback_function(void);

typedef enum unsigned_state plain_unsigned_callback_function(void);
typedef _Atomic(enum unsigned_state)
    atomic_unsigned_callback_function(void);
typedef _Atomic(unsigned int)
    atomic_unsigned_int_callback_function(void);

typedef int plain_signed_consumer_function(
    plain_signed_callback_function *callback);
typedef int atomic_signed_consumer_function(
    atomic_signed_callback_function *callback);
typedef int plain_unsigned_consumer_function(
    plain_unsigned_callback_function *callback);
typedef int atomic_unsigned_consumer_function(
    atomic_unsigned_callback_function *callback);

static enum signed_state plain_signed_callback(void) {
  return SIGNED_STATE_VALUE;
}

static _Atomic(enum signed_state)
atomic_signed_callback(void) {
  return SIGNED_STATE_VALUE;
}

static _Atomic(int) atomic_int_callback(void) {
  return 17;
}

static enum unsigned_state plain_unsigned_callback(void) {
  return UNSIGNED_STATE_VALUE;
}

static _Atomic(enum unsigned_state)
atomic_unsigned_callback(void) {
  return UNSIGNED_STATE_VALUE;
}

static _Atomic(unsigned int)
atomic_unsigned_int_callback(void) {
  return 25u;
}

static int consume_plain_signed(
    plain_signed_callback_function *callback) {
  return callback();
}

static int consume_atomic_int(
    atomic_int_callback_function *callback) {
  _Atomic(int) value = callback();
  return atomic_load(&value);
}

static int consume_plain_unsigned(
    plain_unsigned_callback_function *callback) {
  return (int)callback();
}

static int consume_atomic_unsigned_int(
    atomic_unsigned_int_callback_function *callback) {
  _Atomic(unsigned int) value = callback();
  return (int)atomic_load(&value);
}

int main(void) {
  plain_signed_consumer_function *plain_signed_consumer =
      consume_plain_signed;
  atomic_signed_consumer_function *atomic_signed_consumer =
      consume_atomic_int;
  plain_unsigned_consumer_function *plain_unsigned_consumer =
      consume_plain_unsigned;
  atomic_unsigned_consumer_function *atomic_unsigned_consumer =
      consume_atomic_unsigned_int;
  atomic_signed_callback_function *compatible_signed_callback =
      atomic_int_callback;
  atomic_unsigned_callback_function *compatible_unsigned_callback =
      atomic_unsigned_int_callback;

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
      "plain signed enum callback remains non-atomic");
  _Static_assert(
      CLASSIFY_CONSUMER(&consume_atomic_int) == 2,
      "atomic signed enum accepts its compatible integer callback");
  _Static_assert(
      CLASSIFY_CONSUMER(&consume_plain_unsigned) == 3,
      "plain unsigned enum callback remains non-atomic");
  _Static_assert(
      CLASSIFY_CONSUMER(&consume_atomic_unsigned_int) == 4,
      "atomic unsigned enum accepts its compatible integer callback");

  assert(plain_signed_consumer(plain_signed_callback) == 17);
  assert(atomic_signed_consumer(atomic_signed_callback) == 17);
  assert(atomic_signed_consumer(compatible_signed_callback) == 17);
  assert(plain_unsigned_consumer(plain_unsigned_callback) == 25);
  assert(atomic_unsigned_consumer(atomic_unsigned_callback) == 25);
  assert(atomic_unsigned_consumer(compatible_unsigned_callback) == 25);
  return 0;
}
