#include <assert.h>
#include <stdatomic.h>

#define CLASSIFY_CONSUMER(expression) \
  _Generic((expression), \
           plain_consumer_function *: 1, \
           atomic_consumer_function *: 2, \
           default: 0)

union words3 {
  unsigned int words[3];
  unsigned char bytes[12];
};

struct guarded_atomic_argument {
  unsigned int before;
  _Atomic(union words3) value;
  unsigned int after;
};

typedef unsigned int plain_callback_function(union words3 value);
typedef unsigned int atomic_callback_function(
    _Atomic(union words3) value);
typedef unsigned int plain_consumer_function(
    plain_callback_function *callback);
typedef unsigned int atomic_consumer_function(
    atomic_callback_function *callback);

_Static_assert(sizeof(union words3) == 12,
               "plain union must use twelve-byte storage");
_Static_assert(sizeof(_Atomic(union words3)) == 16,
               "atomic union must use sixteen-byte storage");

#ifdef __wasm32__
_Static_assert(_Alignof(_Atomic(union words3)) == 8,
               "wasm32 atomic union alignment");
#else
_Static_assert(_Alignof(_Atomic(union words3)) == 16,
               "native atomic union alignment");
#endif

static unsigned int sum_words(union words3 value) {
  return value.words[0] + value.words[1] + value.words[2];
}

static unsigned int inspect_plain(union words3 value) {
  return sum_words(value);
}

static unsigned int inspect_atomic(_Atomic(union words3) value) {
  union words3 snapshot = atomic_load(&value);

  atomic_store(
      &value, ((union words3){.words = {1, 2, 3}}));
  assert(sum_words(atomic_load(&value)) == 6);
  return sum_words(snapshot);
}

static unsigned int consume_plain(
    plain_callback_function *callback) {
  return callback((union words3){.words = {19, 11, 12}});
}

static unsigned int consume_atomic(
    atomic_callback_function *callback) {
  struct guarded_atomic_argument frame;
  union words3 snapshot;
  unsigned int result;

  frame.before = 0x11223344u;
  frame.after = 0x55667788u;
  atomic_init(
      &frame.value,
      ((union words3){.words = {17, 13, 12}}));
  result = callback(frame.value);
  snapshot = atomic_load(&frame.value);

  assert(frame.before == 0x11223344u);
  assert(frame.after == 0x55667788u);
  assert(sum_words(snapshot) == 42);
  return result;
}

int main(void) {
  plain_consumer_function *plain_consumer = consume_plain;
  atomic_consumer_function *atomic_consumer = consume_atomic;

  _Static_assert(
      CLASSIFY_CONSUMER(&consume_plain) == 1,
      "plain large union callback parameter remains non-atomic");
  _Static_assert(
      CLASSIFY_CONSUMER(&consume_atomic) == 2,
      "large atomic union callback parameter remains distinct");

  assert(plain_consumer(inspect_plain) == 42);
  assert(atomic_consumer(inspect_atomic) == 42);
  return 0;
}
