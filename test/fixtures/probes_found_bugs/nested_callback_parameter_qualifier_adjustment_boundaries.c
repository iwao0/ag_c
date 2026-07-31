#include <assert.h>

#define CLASSIFY_ELEMENT_CONSUMER(expression) \
  _Generic((expression), \
           plain_element_consumer_function *: 1, \
           atomic_element_consumer_function *: 2, \
           default: 0)

#define IS_TYPE(expression, type) \
  _Generic((expression), type: 1, default: 0)

typedef int restrict_array_callback_function(
    int values[static const restrict 1]);
typedef int atomic_pointer_array_callback_function(
    int values[_Atomic 1]);
typedef int plain_pointer_callback_function(int *values);
typedef int atomic_element_callback_function(
    _Atomic(int) values[1]);

typedef int restrict_consumer_function(
    restrict_array_callback_function *callback, int *values);
typedef int atomic_pointer_consumer_function(
    atomic_pointer_array_callback_function *callback, int *values);
typedef int plain_consumer_function(
    plain_pointer_callback_function *callback, int *values);
typedef int plain_element_consumer_function(
    plain_pointer_callback_function *callback);
typedef int atomic_element_consumer_function(
    atomic_element_callback_function *callback);

static int read_plain(int *values) {
  return values[0];
}

static int read_atomic(_Atomic(int) *values) {
  return values[0];
}

int consume_restrict_array(
    restrict_array_callback_function *callback, int *values);
static restrict_consumer_function *saved_restrict_consumer =
    consume_restrict_array;

int consume_atomic_pointer_array(
    atomic_pointer_array_callback_function *callback, int *values);
static atomic_pointer_consumer_function *saved_atomic_pointer_consumer =
    consume_atomic_pointer_array;

int consume_restrict_array(
    plain_pointer_callback_function *callback, int *values) {
  return callback(values);
}

int consume_atomic_pointer_array(
    plain_pointer_callback_function *callback, int *values) {
  return callback(values);
}

static int consume_plain_element(
    plain_pointer_callback_function *callback) {
  int values[1] = {42};
  return callback(values);
}

static int consume_atomic_element(
    atomic_element_callback_function *callback) {
  _Atomic(int) values[1] = {42};
  return callback(values);
}

int main(void) {
  int values[1] = {42};
  plain_consumer_function *plain_restrict_consumer =
      consume_restrict_array;
  plain_consumer_function *plain_atomic_pointer_consumer =
      consume_atomic_pointer_array;

  _Static_assert(
      IS_TYPE(
          1 ? saved_restrict_consumer : plain_restrict_consumer,
          plain_consumer_function *),
      "nested restrict array parameter adjusts to a plain pointer");
  _Static_assert(
      IS_TYPE(
          1 ? saved_atomic_pointer_consumer
            : plain_atomic_pointer_consumer,
          plain_consumer_function *),
      "nested atomic-qualified array pointer adjusts to a plain pointer");
  _Static_assert(
      CLASSIFY_ELEMENT_CONSUMER(&consume_plain_element) == 1,
      "plain array element remains non-atomic");
  _Static_assert(
      CLASSIFY_ELEMENT_CONSUMER(&consume_atomic_element) == 2,
      "atomic array element remains distinct after pointer adjustment");

  assert(saved_restrict_consumer(read_plain, values) == 42);
  assert(plain_restrict_consumer(read_plain, values) == 42);
  assert(saved_atomic_pointer_consumer(read_plain, values) == 42);
  assert(plain_atomic_pointer_consumer(read_plain, values) == 42);
  assert(consume_plain_element(read_plain) == 42);
  assert(consume_atomic_element(read_atomic) == 42);
  return 0;
}
