#include <assert.h>

#define CLASSIFY_CONSUMER(expression) \
  _Generic((expression), \
           plain_consumer_function *: 1, \
           qualified_consumer_function *: 2, \
           volatile_consumer_function *: 3, \
           default: 0)

#define CLASSIFY_POINTER_CONSUMER(expression) \
  _Generic((expression), \
           plain_pointer_consumer_function *: 1, \
           qualified_pointer_consumer_function *: 2, \
           pointee_qualified_pointer_consumer_function *: 3, \
           default: 0)

typedef int plain_callback_function(void);
typedef const int qualified_callback_function(void);
typedef volatile int volatile_callback_function(void);
typedef int *plain_pointer_callback_function(void);
typedef int *const qualified_pointer_callback_function(void);
typedef const int *pointee_qualified_pointer_callback_function(void);

typedef int plain_consumer_function(plain_callback_function *callback);
typedef int qualified_consumer_function(
    qualified_callback_function *callback);
typedef int volatile_consumer_function(volatile_callback_function *callback);
typedef int plain_pointer_consumer_function(
    plain_pointer_callback_function *callback);
typedef int qualified_pointer_consumer_function(
    qualified_pointer_callback_function *callback);
typedef int pointee_qualified_pointer_consumer_function(
    pointee_qualified_pointer_callback_function *callback);

static int pointer_value = 53;

static int plain_callback(void) {
  return 17;
}

static const int qualified_callback(void) {
  return 42;
}

static volatile int volatile_callback(void) {
  return 29;
}

static int *plain_pointer_callback(void) {
  return &pointer_value;
}

static int *const qualified_pointer_callback(void) {
  return &pointer_value;
}

static const int *pointee_qualified_pointer_callback(void) {
  return &pointer_value;
}

static int consume_qualified(
    qualified_callback_function *const callback);

static int consume_plain(plain_callback_function *callback) {
  return callback();
}

static int consume_qualified(qualified_callback_function *callback) {
  return callback();
}

static int consume_volatile(volatile_callback_function *callback) {
  return callback();
}

static int consume_plain_pointer(
    plain_pointer_callback_function *callback) {
  return *callback();
}

static int consume_qualified_pointer(
    qualified_pointer_callback_function *const callback);

static int consume_qualified_pointer(
    qualified_pointer_callback_function *callback) {
  return *callback();
}

static int consume_pointee_qualified_pointer(
    pointee_qualified_pointer_callback_function *callback) {
  return *callback();
}

int main(void) {
  plain_consumer_function *plain_consumer = consume_plain;
  qualified_consumer_function *qualified_consumer = consume_qualified;
  volatile_consumer_function *volatile_consumer = consume_volatile;
  plain_pointer_consumer_function *plain_pointer_consumer =
      consume_plain_pointer;
  qualified_pointer_consumer_function *qualified_pointer_consumer =
      consume_qualified_pointer;
  pointee_qualified_pointer_consumer_function *pointee_qualified_consumer =
      consume_pointee_qualified_pointer;

  _Static_assert(
      CLASSIFY_CONSUMER((plain_consumer_function *)0) == 1,
      "plain nested callback return type remains unqualified");
  _Static_assert(
      CLASSIFY_CONSUMER((qualified_consumer_function *)0) == 2,
      "qualified nested callback return type remains distinct");
  _Static_assert(
      CLASSIFY_CONSUMER((volatile_consumer_function *)0) == 3,
      "volatile nested callback return type remains distinct");
  _Static_assert(
      CLASSIFY_CONSUMER(&consume_plain) == 1,
      "plain consumer designator retains its callback type");
  _Static_assert(
      CLASSIFY_CONSUMER(&consume_qualified) == 2,
      "qualified consumer designator retains its callback type");
  _Static_assert(
      CLASSIFY_CONSUMER(&consume_volatile) == 3,
      "volatile consumer designator retains its callback type");
  _Static_assert(
      CLASSIFY_POINTER_CONSUMER(
          (plain_pointer_consumer_function *)0) == 1,
      "plain nested callback pointer return type remains unqualified");
  _Static_assert(
      CLASSIFY_POINTER_CONSUMER(
          (qualified_pointer_consumer_function *)0) == 2,
      "nested callback pointer return qualifier remains distinct");
  _Static_assert(
      CLASSIFY_POINTER_CONSUMER(
          (pointee_qualified_pointer_consumer_function *)0) == 3,
      "nested callback pointee qualifier remains distinct");
  _Static_assert(
      CLASSIFY_POINTER_CONSUMER(&consume_plain_pointer) == 1,
      "plain pointer consumer designator retains its callback type");
  _Static_assert(
      CLASSIFY_POINTER_CONSUMER(&consume_qualified_pointer) == 2,
      "qualified pointer consumer designator retains its callback type");
  _Static_assert(
      CLASSIFY_POINTER_CONSUMER(
          &consume_pointee_qualified_pointer) == 3,
      "pointee-qualified consumer designator retains its callback type");

  assert(plain_consumer(plain_callback) == 17);
  assert(qualified_consumer(qualified_callback) == 42);
  assert(volatile_consumer(volatile_callback) == 29);
  assert(plain_pointer_consumer(plain_pointer_callback) == 53);
  assert(qualified_pointer_consumer(qualified_pointer_callback) == 53);
  assert(pointee_qualified_consumer(
             pointee_qualified_pointer_callback) == 53);
  return 0;
}
