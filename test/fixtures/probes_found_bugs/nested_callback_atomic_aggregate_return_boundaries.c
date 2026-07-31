#include <assert.h>
#include <stdatomic.h>

#define CLASSIFY_CONSUMER(expression) \
  _Generic((expression), \
           plain_consumer_function *: 1, \
           atomic_consumer_function *: 2, \
           default: 0)

struct pair {
  int left;
  int right;
};

typedef struct pair plain_callback_function(void);
typedef _Atomic(struct pair) atomic_callback_function(void);
typedef int plain_consumer_function(plain_callback_function *callback);
typedef int atomic_consumer_function(atomic_callback_function *callback);

static struct pair plain_callback(void) {
  return (struct pair){19, 23};
}

static _Atomic(struct pair) atomic_callback(void) {
  return (struct pair){17, 25};
}

static int sum_pair(struct pair value) {
  return value.left + value.right;
}

static int consume_plain(plain_callback_function *callback) {
  return sum_pair(callback());
}

static int consume_atomic(atomic_callback_function *callback) {
  _Atomic(struct pair) atomic_value = callback();
  struct pair value = atomic_load(&atomic_value);
  return sum_pair(value);
}

int main(void) {
  plain_consumer_function *plain_consumer = consume_plain;
  atomic_consumer_function *atomic_consumer = consume_atomic;

  _Static_assert(
      CLASSIFY_CONSUMER(&consume_plain) == 1,
      "plain aggregate callback return remains non-atomic");
  _Static_assert(
      CLASSIFY_CONSUMER(&consume_atomic) == 2,
      "atomic aggregate callback return remains distinct");

  assert(plain_consumer(plain_callback) == 42);
  assert(atomic_consumer(atomic_callback) == 42);
  return 0;
}
