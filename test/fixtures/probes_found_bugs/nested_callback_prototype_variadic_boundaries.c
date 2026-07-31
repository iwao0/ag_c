#include <assert.h>

#define CLASSIFY_VARIADIC_CONSUMER(expression) \
  _Generic((expression), \
           fixed_consumer_function *: 1, \
           variadic_consumer_function *: 2, \
           default: 0)

#define IS_TYPE(expression, type) \
  _Generic((expression), type: 1, default: 0)

typedef int unprototyped_callback_function();
typedef int prototyped_callback_function(int value);
typedef int fixed_callback_function(int value);
typedef int variadic_callback_function(int value, ...);

typedef int unprototyped_consumer_function(
    unprototyped_callback_function *callback, int value);
typedef int prototyped_consumer_function(
    prototyped_callback_function *callback, int value);
typedef int fixed_consumer_function(
    fixed_callback_function *callback, int value);
typedef int variadic_consumer_function(
    variadic_callback_function *callback, int value);

static int add_two(int value) {
  return value + 2;
}

static int add_two_variadic(int value, ...) {
  return value + 2;
}

int consume_unprototyped(
    unprototyped_callback_function *callback, int value);
static unprototyped_consumer_function *saved_unprototyped_consumer =
    consume_unprototyped;

int consume_unprototyped(
    prototyped_callback_function *callback, int value) {
  return callback(value);
}

static int consume_fixed(
    fixed_callback_function *callback, int value) {
  return callback(value);
}

static int consume_variadic(
    variadic_callback_function *callback, int value) {
  return callback(value, 0);
}

int main(void) {
  prototyped_consumer_function *prototyped_consumer =
      consume_unprototyped;
  fixed_consumer_function *fixed_consumer = consume_fixed;
  variadic_consumer_function *variadic_consumer = consume_variadic;

  _Static_assert(
      IS_TYPE(
          1 ? saved_unprototyped_consumer : prototyped_consumer,
          prototyped_consumer_function *),
      "nested unprototyped callback is compatible with an int prototype");
  _Static_assert(
      CLASSIFY_VARIADIC_CONSUMER(&consume_fixed) == 1,
      "fixed nested callback remains non-variadic");
  _Static_assert(
      CLASSIFY_VARIADIC_CONSUMER(&consume_variadic) == 2,
      "variadic nested callback remains distinct");

  assert(saved_unprototyped_consumer(add_two, 40) == 42);
  assert(prototyped_consumer(add_two, 40) == 42);
  assert(fixed_consumer(add_two, 40) == 42);
  assert(variadic_consumer(add_two_variadic, 40) == 42);
  return 0;
}
