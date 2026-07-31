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

struct guarded_atomic_bool {
  unsigned char before;
  _Atomic(_Bool) value;
  unsigned char after;
};

struct guarded_atomic_signed_char {
  unsigned char before;
  _Atomic(signed char) value;
  unsigned char after;
};

struct guarded_atomic_unsigned_short {
  unsigned short before;
  _Atomic(unsigned short) value;
  unsigned short after;
};

typedef int plain_bool_callback_function(_Bool value);
typedef int atomic_bool_callback_function(
    _Atomic(_Bool) value);
typedef int plain_signed_char_callback_function(
    signed char value);
typedef int atomic_signed_char_callback_function(
    _Atomic(signed char) value);
typedef int plain_unsigned_short_callback_function(
    unsigned short value);
typedef int atomic_unsigned_short_callback_function(
    _Atomic(unsigned short) value);

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

static int inspect_plain_bool(_Bool value) {
  return value;
}

static int inspect_atomic_bool(_Atomic(_Bool) value) {
  _Bool snapshot = atomic_load(&value);

  atomic_store(&value, 0);
  assert(atomic_load(&value) == 0);
  return snapshot;
}

static int inspect_plain_signed_char(signed char value) {
  return value;
}

static int inspect_atomic_signed_char(
    _Atomic(signed char) value) {
  signed char snapshot = atomic_load(&value);

  atomic_store(&value, 19);
  assert(atomic_load(&value) == 19);
  return snapshot;
}

static int inspect_plain_unsigned_short(unsigned short value) {
  return value;
}

static int inspect_atomic_unsigned_short(
    _Atomic(unsigned short) value) {
  unsigned short snapshot = atomic_load(&value);

  atomic_store(&value, 42u);
  assert(atomic_load(&value) == 42);
  return snapshot;
}

static int consume_plain_bool(
    plain_bool_callback_function *callback) {
  return callback(3);
}

static int consume_atomic_bool(
    atomic_bool_callback_function *callback) {
  struct guarded_atomic_bool frame;
  _Bool snapshot;
  int result;

  frame.before = 0x5au;
  frame.after = 0xa5u;
  atomic_init(&frame.value, 7);
  result = callback(frame.value);
  snapshot = atomic_load(&frame.value);

  assert(frame.before == 0x5a);
  assert(frame.after == 0xa5);
  assert(snapshot == 1);
  return result;
}

static int consume_plain_signed_char(
    plain_signed_char_callback_function *callback) {
  return callback(-17);
}

static int consume_atomic_signed_char(
    atomic_signed_char_callback_function *callback) {
  struct guarded_atomic_signed_char frame;
  signed char snapshot;
  int result;

  frame.before = 0x3cu;
  frame.after = 0xc3u;
  atomic_init(&frame.value, -17);
  result = callback(frame.value);
  snapshot = atomic_load(&frame.value);

  assert(frame.before == 0x3c);
  assert(frame.after == 0xc3);
  assert(snapshot == -17);
  return result;
}

static int consume_plain_unsigned_short(
    plain_unsigned_short_callback_function *callback) {
  return callback(65000u);
}

static int consume_atomic_unsigned_short(
    atomic_unsigned_short_callback_function *callback) {
  struct guarded_atomic_unsigned_short frame;
  unsigned short snapshot;
  int result;

  frame.before = 0x1122u;
  frame.after = 0x7788u;
  atomic_init(&frame.value, 65000u);
  result = callback(frame.value);
  snapshot = atomic_load(&frame.value);

  assert(frame.before == 0x1122);
  assert(frame.after == 0x7788);
  assert(snapshot == 65000);
  return result;
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
      "plain bool callback parameter remains non-atomic");
  _Static_assert(
      CLASSIFY_CONSUMER(&consume_atomic_bool) == 2,
      "atomic bool callback parameter remains distinct");
  _Static_assert(
      CLASSIFY_CONSUMER(&consume_plain_signed_char) == 3,
      "plain signed char callback parameter remains non-atomic");
  _Static_assert(
      CLASSIFY_CONSUMER(&consume_atomic_signed_char) == 4,
      "atomic signed char callback parameter remains distinct");
  _Static_assert(
      CLASSIFY_CONSUMER(&consume_plain_unsigned_short) == 5,
      "plain unsigned short callback parameter remains non-atomic");
  _Static_assert(
      CLASSIFY_CONSUMER(&consume_atomic_unsigned_short) == 6,
      "atomic unsigned short callback parameter remains distinct");

  assert(plain_bool_consumer(inspect_plain_bool) == 1);
  assert(atomic_bool_consumer(inspect_atomic_bool) == 1);
  assert(plain_signed_char_consumer(
             inspect_plain_signed_char) == -17);
  assert(atomic_signed_char_consumer(
             inspect_atomic_signed_char) == -17);
  assert(plain_unsigned_short_consumer(
             inspect_plain_unsigned_short) == 65000);
  assert(atomic_unsigned_short_consumer(
             inspect_atomic_unsigned_short) == 65000);
  return 0;
}
