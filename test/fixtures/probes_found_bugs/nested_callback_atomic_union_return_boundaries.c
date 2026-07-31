#include <assert.h>
#include <stdatomic.h>

#define CLASSIFY_CONSUMER(expression) \
  _Generic((expression), \
           plain_word_consumer_function *: 1, \
           atomic_word_consumer_function *: 2, \
           plain_wide_consumer_function *: 3, \
           atomic_wide_consumer_function *: 4, \
           default: 0)

union word {
  unsigned int bits;
  float value;
};

union wide {
  unsigned long long words[2];
  unsigned char bytes[16];
};

struct guarded_word {
  unsigned int before;
  _Atomic(union word) value;
  unsigned int after;
};

struct guarded_wide {
  unsigned long long before;
  _Atomic(union wide) value;
  unsigned long long after;
};

typedef union word plain_word_callback_function(void);
typedef _Atomic(union word) atomic_word_callback_function(void);
typedef union wide plain_wide_callback_function(void);
typedef _Atomic(union wide) atomic_wide_callback_function(void);

typedef int plain_word_consumer_function(
    plain_word_callback_function *callback);
typedef int atomic_word_consumer_function(
    atomic_word_callback_function *callback);
typedef int plain_wide_consumer_function(
    plain_wide_callback_function *callback);
typedef int atomic_wide_consumer_function(
    atomic_wide_callback_function *callback);

_Static_assert(sizeof(_Atomic(union word)) == 4,
               "atomic word storage size");
_Static_assert(sizeof(_Atomic(union wide)) == 16,
               "atomic wide union storage size");

#ifdef __wasm32__
_Static_assert(_Alignof(_Atomic(union wide)) == 8,
               "wasm32 atomic wide union alignment");
#else
_Static_assert(_Alignof(_Atomic(union wide)) == 16,
               "native atomic wide union alignment");
#endif

static union word plain_word_callback(void) {
  return (union word){.bits = 42u};
}

static _Atomic(union word) atomic_word_callback(void) {
  return (union word){.bits = 42u};
}

static union wide plain_wide_callback(void) {
  return (union wide){.words = {19u, 23u}};
}

static _Atomic(union wide) atomic_wide_callback(void) {
  return (union wide){.words = {17u, 25u}};
}

static int consume_plain_word(
    plain_word_callback_function *callback) {
  union word value = callback();

  assert(value.bits == 42u);
  return (int)value.bits;
}

static int consume_atomic_word(
    atomic_word_callback_function *callback) {
  struct guarded_word frame;
  union word snapshot;

  frame.before = 0x11223344u;
  frame.after = 0x55667788u;
  atomic_init(&frame.value, ((union word){.bits = 0u}));
  frame.value = callback();
  snapshot = atomic_load(&frame.value);

  assert(frame.before == 0x11223344u);
  assert(frame.after == 0x55667788u);
  assert(snapshot.bits == 42u);
  return (int)snapshot.bits;
}

static int consume_plain_wide(
    plain_wide_callback_function *callback) {
  union wide value = callback();

  assert(value.words[0] == 19u);
  assert(value.words[1] == 23u);
  return (int)(value.words[0] + value.words[1]);
}

static int consume_atomic_wide(
    atomic_wide_callback_function *callback) {
  struct guarded_wide frame;
  union wide snapshot;

  frame.before = 0x1122334455667788ULL;
  frame.after = 0x8877665544332211ULL;
  atomic_init(
      &frame.value,
      ((union wide){.words = {0u, 0u}}));
  frame.value = callback();
  snapshot = atomic_load(&frame.value);

  assert(frame.before == 0x1122334455667788ULL);
  assert(frame.after == 0x8877665544332211ULL);
  assert(snapshot.words[0] == 17u);
  assert(snapshot.words[1] == 25u);
  return (int)(snapshot.words[0] + snapshot.words[1]);
}

int main(void) {
  plain_word_consumer_function *plain_word_consumer =
      consume_plain_word;
  atomic_word_consumer_function *atomic_word_consumer =
      consume_atomic_word;
  plain_wide_consumer_function *plain_wide_consumer =
      consume_plain_wide;
  atomic_wide_consumer_function *atomic_wide_consumer =
      consume_atomic_wide;

  _Static_assert(
      CLASSIFY_CONSUMER(&consume_plain_word) == 1,
      "plain word union callback remains non-atomic");
  _Static_assert(
      CLASSIFY_CONSUMER(&consume_atomic_word) == 2,
      "atomic word union callback remains distinct");
  _Static_assert(
      CLASSIFY_CONSUMER(&consume_plain_wide) == 3,
      "plain wide union callback remains non-atomic");
  _Static_assert(
      CLASSIFY_CONSUMER(&consume_atomic_wide) == 4,
      "atomic wide union callback remains distinct");

  assert(plain_word_consumer(plain_word_callback) == 42);
  assert(atomic_word_consumer(atomic_word_callback) == 42);
  assert(plain_wide_consumer(plain_wide_callback) == 42);
  assert(atomic_wide_consumer(atomic_wide_callback) == 42);
  return 0;
}
