#include <assert.h>
#include <stdatomic.h>

#define CLASSIFY_CONSUMER(expression) \
  _Generic((expression), \
           plain_bool_consumer_function *: 1, \
           atomic_bool_consumer_function *: 2, \
           plain_signed_char_consumer_function *: 3, \
           atomic_signed_char_consumer_function *: 4, \
           plain_unsigned_short_consumer_function *: 5, \
           atomic_unsigned_short_consumer_function *: 6, \
           default: 0)

struct guarded_bool {
  unsigned char before;
  _Atomic(_Bool) value;
  unsigned char after;
};

struct guarded_signed_char {
  unsigned char before;
  _Atomic(signed char) value;
  unsigned char after;
};

struct guarded_unsigned_short {
  unsigned short before;
  _Atomic(unsigned short) value;
  unsigned short after;
};

typedef _Bool plain_bool_callback_function(void);
typedef _Atomic(_Bool) atomic_bool_callback_function(void);
typedef signed char plain_signed_char_callback_function(void);
typedef _Atomic(signed char)
    atomic_signed_char_callback_function(void);
typedef unsigned short
    plain_unsigned_short_callback_function(void);
typedef _Atomic(unsigned short)
    atomic_unsigned_short_callback_function(void);

typedef int plain_bool_consumer_function(
    plain_bool_callback_function *callback);
typedef int atomic_bool_consumer_function(
    atomic_bool_callback_function *callback);
typedef int plain_signed_char_consumer_function(
    plain_signed_char_callback_function *callback);
typedef int atomic_signed_char_consumer_function(
    atomic_signed_char_callback_function *callback);
typedef int plain_unsigned_short_consumer_function(
    plain_unsigned_short_callback_function *callback);
typedef int atomic_unsigned_short_consumer_function(
    atomic_unsigned_short_callback_function *callback);

_Static_assert(sizeof(_Atomic(_Bool)) == 1,
               "atomic bool storage size");
_Static_assert(sizeof(_Atomic(signed char)) == 1,
               "atomic signed char storage size");
_Static_assert(sizeof(_Atomic(unsigned short)) == 2,
               "atomic unsigned short storage size");

static _Bool plain_bool_callback(void) {
  return 3;
}

static _Atomic(_Bool) atomic_bool_callback(void) {
  return 7;
}

static signed char plain_signed_char_callback(void) {
  return -17;
}

static _Atomic(signed char)
atomic_signed_char_callback(void) {
  return -17;
}

static unsigned short plain_unsigned_short_callback(void) {
  return 65000u;
}

static _Atomic(unsigned short)
atomic_unsigned_short_callback(void) {
  return 65000u;
}

static int consume_plain_bool(
    plain_bool_callback_function *callback) {
  return callback();
}

static int consume_atomic_bool(
    atomic_bool_callback_function *callback) {
  struct guarded_bool frame;
  _Bool snapshot;

  frame.before = 0x5au;
  frame.after = 0xa5u;
  atomic_init(&frame.value, 0);
  frame.value = callback();
  snapshot = atomic_load(&frame.value);

  assert(frame.before == 0x5a);
  assert(frame.after == 0xa5);
  return snapshot;
}

static int consume_plain_signed_char(
    plain_signed_char_callback_function *callback) {
  return callback();
}

static int consume_atomic_signed_char(
    atomic_signed_char_callback_function *callback) {
  struct guarded_signed_char frame;
  signed char snapshot;

  frame.before = 0x5au;
  frame.after = 0xa5u;
  atomic_init(&frame.value, 0);
  frame.value = callback();
  snapshot = atomic_load(&frame.value);

  assert(frame.before == 0x5a);
  assert(frame.after == 0xa5);
  return snapshot;
}

static int consume_plain_unsigned_short(
    plain_unsigned_short_callback_function *callback) {
  return callback();
}

static int consume_atomic_unsigned_short(
    atomic_unsigned_short_callback_function *callback) {
  struct guarded_unsigned_short frame;
  unsigned short snapshot;

  frame.before = 0x1122u;
  frame.after = 0x7788u;
  atomic_init(&frame.value, 0);
  frame.value = callback();
  snapshot = atomic_load(&frame.value);

  assert(frame.before == 0x1122);
  assert(frame.after == 0x7788);
  return snapshot;
}

int main(void) {
  plain_bool_consumer_function *plain_bool_consumer =
      consume_plain_bool;
  atomic_bool_consumer_function *atomic_bool_consumer =
      consume_atomic_bool;
  plain_signed_char_consumer_function *plain_signed_char_consumer =
      consume_plain_signed_char;
  atomic_signed_char_consumer_function *atomic_signed_char_consumer =
      consume_atomic_signed_char;
  plain_unsigned_short_consumer_function
      *plain_unsigned_short_consumer =
          consume_plain_unsigned_short;
  atomic_unsigned_short_consumer_function
      *atomic_unsigned_short_consumer =
          consume_atomic_unsigned_short;

  _Static_assert(
      CLASSIFY_CONSUMER(&consume_plain_bool) == 1,
      "plain bool callback remains non-atomic");
  _Static_assert(
      CLASSIFY_CONSUMER(&consume_atomic_bool) == 2,
      "atomic bool callback remains distinct");
  _Static_assert(
      CLASSIFY_CONSUMER(&consume_plain_signed_char) == 3,
      "plain signed char callback remains non-atomic");
  _Static_assert(
      CLASSIFY_CONSUMER(&consume_atomic_signed_char) == 4,
      "atomic signed char callback remains distinct");
  _Static_assert(
      CLASSIFY_CONSUMER(&consume_plain_unsigned_short) == 5,
      "plain unsigned short callback remains non-atomic");
  _Static_assert(
      CLASSIFY_CONSUMER(&consume_atomic_unsigned_short) == 6,
      "atomic unsigned short callback remains distinct");

  assert(plain_bool_consumer(plain_bool_callback) == 1);
  assert(atomic_bool_consumer(atomic_bool_callback) == 1);
  assert(plain_signed_char_consumer(
             plain_signed_char_callback) == -17);
  assert(atomic_signed_char_consumer(
             atomic_signed_char_callback) == -17);
  assert(plain_unsigned_short_consumer(
             plain_unsigned_short_callback) == 65000);
  assert(atomic_unsigned_short_consumer(
             atomic_unsigned_short_callback) == 65000);
  return 0;
}
