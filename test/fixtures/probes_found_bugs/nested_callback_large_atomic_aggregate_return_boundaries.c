#include <assert.h>
#include <stdatomic.h>

#define CLASSIFY_CONSUMER(expression) \
  _Generic((expression), \
           plain_consumer_function *: 1, \
           atomic_consumer_function *: 2, \
           default: 0)

struct words3 {
  unsigned int first;
  unsigned int second;
  unsigned int third;
};

struct guarded_atomic_result {
  unsigned int before;
  _Atomic(struct words3) value;
  unsigned int after;
};

typedef struct words3 plain_callback_function(void);
typedef _Atomic(struct words3) atomic_callback_function(void);
typedef unsigned int plain_consumer_function(
    plain_callback_function *callback);
typedef unsigned int atomic_consumer_function(
    atomic_callback_function *callback);

_Static_assert(sizeof(struct words3) == 12,
               "plain aggregate must use twelve-byte storage");
_Static_assert(sizeof(_Atomic(struct words3)) == 16,
               "atomic aggregate must use sixteen-byte storage");

#ifdef __wasm32__
_Static_assert(_Alignof(_Atomic(struct words3)) == 8,
               "wasm32 atomic aggregate alignment");
#else
_Static_assert(_Alignof(_Atomic(struct words3)) == 16,
               "native atomic aggregate alignment");
#endif

static struct words3 plain_callback(void) {
  return (struct words3){19, 11, 12};
}

static _Atomic(struct words3) atomic_callback(void) {
  return (struct words3){17, 13, 12};
}

static unsigned int sum_words(struct words3 value) {
  return value.first + value.second + value.third;
}

static unsigned int consume_plain(
    plain_callback_function *callback) {
  return sum_words(callback());
}

static unsigned int consume_atomic(
    atomic_callback_function *callback) {
  struct guarded_atomic_result frame;
  struct words3 snapshot;

  frame.before = 0x11223344u;
  frame.after = 0x55667788u;
  atomic_init(&frame.value, ((struct words3){0, 0, 0}));
  frame.value = callback();
  snapshot = atomic_load(&frame.value);

  assert(frame.before == 0x11223344u);
  assert(frame.after == 0x55667788u);
  return sum_words(snapshot);
}

int main(void) {
  plain_consumer_function *plain_consumer = consume_plain;
  atomic_consumer_function *atomic_consumer = consume_atomic;

  _Static_assert(
      CLASSIFY_CONSUMER(&consume_plain) == 1,
      "plain large aggregate callback remains non-atomic");
  _Static_assert(
      CLASSIFY_CONSUMER(&consume_atomic) == 2,
      "large atomic aggregate callback remains distinct");

  assert(plain_consumer(plain_callback) == 42);
  assert(atomic_consumer(atomic_callback) == 42);
  return 0;
}
