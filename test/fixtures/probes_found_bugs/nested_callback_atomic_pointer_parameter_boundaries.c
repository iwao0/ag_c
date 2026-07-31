#include <assert.h>
#include <stdatomic.h>

#define CLASSIFY_CONSUMER(expression) \
  _Generic((expression), \
           plain_pointer_consumer_function *: 1, \
           atomic_pointer_consumer_function *: 2, \
           atomic_pointee_consumer_function *: 3, \
           default: 0)

struct guarded_atomic_pointer {
  unsigned long long before;
  _Atomic(int *) value;
  unsigned long long after;
};

typedef int plain_pointer_callback_function(int *value);
typedef int atomic_pointer_callback_function(
    _Atomic(int *) value);
typedef int atomic_pointee_callback_function(
    _Atomic(int) *value);

typedef int plain_pointer_consumer_function(
    plain_pointer_callback_function *callback);
typedef int atomic_pointer_consumer_function(
    atomic_pointer_callback_function *callback);
typedef int atomic_pointee_consumer_function(
    atomic_pointee_callback_function *callback);

static int inspect_plain_pointer(int *value) {
  assert(*value == 42);
  return *value;
}

static int inspect_atomic_pointer(_Atomic(int *) value) {
  int replacement = 6;
  int *snapshot = atomic_load(&value);

  atomic_store(&value, &replacement);
  assert(*atomic_load(&value) == 6);
  return *snapshot;
}

static int inspect_atomic_pointee(_Atomic(int) *value) {
  int snapshot = atomic_load(value);

  atomic_store(value, 6);
  assert(atomic_load(value) == 6);
  return snapshot;
}

static int consume_plain_pointer(
    plain_pointer_callback_function *callback) {
  int value = 42;

  return callback(&value);
}

static int consume_atomic_pointer(
    atomic_pointer_callback_function *callback) {
  struct guarded_atomic_pointer frame;
  int value = 42;
  int *snapshot;
  int result;

  frame.before = 0x1122334455667788ULL;
  frame.after = 0x8877665544332211ULL;
  atomic_init(&frame.value, &value);
  result = callback(frame.value);
  snapshot = atomic_load(&frame.value);

  assert(frame.before == 0x1122334455667788ULL);
  assert(frame.after == 0x8877665544332211ULL);
  assert(snapshot == &value);
  assert(*snapshot == 42);
  return result;
}

static int consume_atomic_pointee(
    atomic_pointee_callback_function *callback) {
  _Atomic(int) value = 42;

  assert(callback(&value) == 42);
  return atomic_load(&value);
}

int main(void) {
  plain_pointer_consumer_function *plain_pointer_consumer =
      consume_plain_pointer;
  atomic_pointer_consumer_function *atomic_pointer_consumer =
      consume_atomic_pointer;
  atomic_pointee_consumer_function *atomic_pointee_consumer =
      consume_atomic_pointee;

  _Static_assert(
      CLASSIFY_CONSUMER(&consume_plain_pointer) == 1,
      "plain pointer callback parameter remains non-atomic");
  _Static_assert(
      CLASSIFY_CONSUMER(&consume_atomic_pointer) == 2,
      "atomic pointer callback parameter remains distinct");
  _Static_assert(
      CLASSIFY_CONSUMER(&consume_atomic_pointee) == 3,
      "pointer to atomic callback parameter remains distinct");

  assert(plain_pointer_consumer(inspect_plain_pointer) == 42);
  assert(atomic_pointer_consumer(inspect_atomic_pointer) == 42);
  assert(atomic_pointee_consumer(inspect_atomic_pointee) == 6);
  return 0;
}
