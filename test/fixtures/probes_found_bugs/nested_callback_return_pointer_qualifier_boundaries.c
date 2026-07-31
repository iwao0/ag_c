#include <assert.h>

#define CLASSIFY_CONSUMER(expression) \
  _Generic((expression), \
           plain_consumer_function *: 1, \
           volatile_pointer_consumer_function *: 2, \
           restrict_pointer_consumer_function *: 3, \
           atomic_pointer_consumer_function *: 4, \
           atomic_pointee_consumer_function *: 5, \
           default: 0)

typedef int *plain_callback_function(void);
typedef int *volatile volatile_pointer_callback_function(void);
typedef int *restrict restrict_pointer_callback_function(void);
typedef _Atomic(int *) atomic_pointer_callback_function(void);
typedef _Atomic(int) *atomic_pointee_callback_function(void);

typedef int plain_consumer_function(plain_callback_function *callback);
typedef int volatile_pointer_consumer_function(
    volatile_pointer_callback_function *callback);
typedef int restrict_pointer_consumer_function(
    restrict_pointer_callback_function *callback);
typedef int atomic_pointer_consumer_function(
    atomic_pointer_callback_function *callback);
typedef int atomic_pointee_consumer_function(
    atomic_pointee_callback_function *callback);

static int plain_value = 42;
static _Atomic(int) atomic_value = 42;

static int *plain_callback(void) {
  return &plain_value;
}

static int *volatile volatile_pointer_callback(void) {
  return &plain_value;
}

static int *restrict restrict_pointer_callback(void) {
  return &plain_value;
}

static _Atomic(int *) atomic_pointer_callback(void) {
  return &plain_value;
}

static _Atomic(int) *atomic_pointee_callback(void) {
  return &atomic_value;
}

static int consume_plain(plain_callback_function *callback) {
  return *callback();
}

static int consume_volatile_pointer(
    volatile_pointer_callback_function *callback) {
  return *callback();
}

static int consume_restrict_pointer(
    restrict_pointer_callback_function *callback) {
  return *callback();
}

static int consume_atomic_pointer(
    atomic_pointer_callback_function *callback) {
  _Atomic(int *) pointer = callback();
  return *pointer;
}

static int consume_atomic_pointee(
    atomic_pointee_callback_function *callback) {
  return *callback();
}

int main(void) {
  plain_consumer_function *plain_consumer = consume_plain;
  volatile_pointer_consumer_function *volatile_consumer =
      consume_volatile_pointer;
  restrict_pointer_consumer_function *restrict_consumer =
      consume_restrict_pointer;
  atomic_pointer_consumer_function *atomic_pointer_consumer =
      consume_atomic_pointer;
  atomic_pointee_consumer_function *atomic_pointee_consumer =
      consume_atomic_pointee;

  _Static_assert(
      CLASSIFY_CONSUMER(&consume_plain) == 1,
      "plain callback pointer return remains unqualified");
  _Static_assert(
      CLASSIFY_CONSUMER(&consume_volatile_pointer) == 2,
      "volatile callback pointer return remains distinct");
  _Static_assert(
      CLASSIFY_CONSUMER(&consume_restrict_pointer) == 3,
      "restrict callback pointer return remains distinct");
  _Static_assert(
      CLASSIFY_CONSUMER(&consume_atomic_pointer) == 4,
      "atomic callback pointer return remains distinct");
  _Static_assert(
      CLASSIFY_CONSUMER(&consume_atomic_pointee) == 5,
      "atomic callback pointee return remains distinct");

  assert(plain_consumer(plain_callback) == 42);
  assert(volatile_consumer(volatile_pointer_callback) == 42);
  assert(restrict_consumer(restrict_pointer_callback) == 42);
  assert(atomic_pointer_consumer(atomic_pointer_callback) == 42);
  assert(atomic_pointee_consumer(atomic_pointee_callback) == 42);
  return 0;
}
