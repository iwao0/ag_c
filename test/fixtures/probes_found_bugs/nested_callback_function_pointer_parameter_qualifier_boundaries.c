#include <assert.h>

#define CLASSIFY_CONSUMER(expression) \
  _Generic((expression), \
           plain_consumer_function *: 1, \
           atomic_consumer_function *: 2, \
           default: 0)

#define IS_TYPE(expression, type) \
  _Generic((expression), type: 1, default: 0)

typedef int target_function(int value);
typedef int plain_callback_function(
    target_function *target, int value);
typedef int const_pointer_callback_function(
    target_function *const target, int value);
typedef int volatile_pointer_callback_function(
    target_function *volatile target, int value);
typedef int atomic_pointer_callback_function(
    _Atomic(target_function *) target, int value);

typedef int plain_consumer_function(
    plain_callback_function *callback,
    target_function *target, int value);
typedef int const_pointer_consumer_function(
    const_pointer_callback_function *callback,
    target_function *target, int value);
typedef int volatile_pointer_consumer_function(
    volatile_pointer_callback_function *callback,
    target_function *target, int value);
typedef int atomic_consumer_function(
    atomic_pointer_callback_function *callback,
    target_function *target, int value);

static int add_two(int value) {
  return value + 2;
}

static int invoke_plain(target_function *target, int value) {
  return target(value);
}

static int invoke_const_pointer(
    target_function *const target, int value) {
  return target(value);
}

static int invoke_volatile_pointer(
    target_function *volatile target, int value) {
  return target(value);
}

static int invoke_atomic_pointer(
    _Atomic(target_function *) target, int value) {
  return target(value);
}

static int consume_plain(
    plain_callback_function *callback,
    target_function *target, int value) {
  return callback(target, value);
}

static int consume_atomic(
    atomic_pointer_callback_function *callback,
    target_function *target, int value) {
  _Atomic(target_function *) atomic_target = target;
  return callback(atomic_target, value);
}

int main(void) {
  plain_consumer_function *plain_consumer = consume_plain;
  const_pointer_consumer_function *const_consumer = consume_plain;
  volatile_pointer_consumer_function *volatile_consumer =
      consume_plain;
  atomic_consumer_function *atomic_consumer = consume_atomic;

  _Static_assert(
      IS_TYPE(1 ? plain_consumer : const_consumer,
              plain_consumer_function *),
      "nested const function pointer parameter is top-level only");
  _Static_assert(
      IS_TYPE(1 ? plain_consumer : volatile_consumer,
              plain_consumer_function *),
      "nested volatile function pointer parameter is top-level only");
  _Static_assert(
      CLASSIFY_CONSUMER(&consume_plain) == 1,
      "plain nested function pointer parameter remains non-atomic");
  _Static_assert(
      CLASSIFY_CONSUMER(&consume_atomic) == 2,
      "atomic nested function pointer parameter remains distinct");

  assert(plain_consumer(invoke_plain, add_two, 40) == 42);
  assert(const_consumer(invoke_const_pointer, add_two, 40) == 42);
  assert(volatile_consumer(
             invoke_volatile_pointer, add_two, 40) == 42);
  assert(atomic_consumer(invoke_atomic_pointer, add_two, 40) == 42);
  return 0;
}
