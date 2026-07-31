#include <assert.h>

#define CLASSIFY_CONSUMER(expression) \
  _Generic((expression), \
           plain_consumer_function *: 1, \
           const_pointer_consumer_function *: 2, \
           volatile_pointer_consumer_function *: 3, \
           atomic_pointer_consumer_function *: 4, \
           default: 0)

typedef int target_function(int value);
typedef target_function *plain_factory_function(void);
typedef target_function *const const_pointer_factory_function(void);
typedef target_function *volatile volatile_pointer_factory_function(void);
typedef _Atomic(target_function *) atomic_pointer_factory_function(void);

typedef int plain_consumer_function(
    plain_factory_function *factory, int value);
typedef int const_pointer_consumer_function(
    const_pointer_factory_function *factory, int value);
typedef int volatile_pointer_consumer_function(
    volatile_pointer_factory_function *factory, int value);
typedef int atomic_pointer_consumer_function(
    atomic_pointer_factory_function *factory, int value);

static int add_two(int value) {
  return value + 2;
}

static target_function *plain_factory(void) {
  return add_two;
}

static target_function *const const_pointer_factory(void) {
  return add_two;
}

static target_function *volatile volatile_pointer_factory(void) {
  return add_two;
}

static _Atomic(target_function *) atomic_pointer_factory(void) {
  return add_two;
}

static int consume_plain(
    plain_factory_function *factory, int value) {
  return factory()(value);
}

static int consume_const_pointer(
    const_pointer_factory_function *factory, int value) {
  return factory()(value);
}

static int consume_volatile_pointer(
    volatile_pointer_factory_function *factory, int value) {
  return factory()(value);
}

static int consume_atomic_pointer(
    atomic_pointer_factory_function *factory, int value) {
  _Atomic(target_function *) callback = factory();
  return callback(value);
}

int main(void) {
  plain_consumer_function *plain_consumer = consume_plain;
  const_pointer_consumer_function *const_consumer =
      consume_const_pointer;
  volatile_pointer_consumer_function *volatile_consumer =
      consume_volatile_pointer;
  atomic_pointer_consumer_function *atomic_consumer =
      consume_atomic_pointer;

  _Static_assert(
      CLASSIFY_CONSUMER(&consume_plain) == 1,
      "plain returned function pointer remains unqualified");
  _Static_assert(
      CLASSIFY_CONSUMER(&consume_const_pointer) == 2,
      "const returned function pointer remains distinct");
  _Static_assert(
      CLASSIFY_CONSUMER(&consume_volatile_pointer) == 3,
      "volatile returned function pointer remains distinct");
  _Static_assert(
      CLASSIFY_CONSUMER(&consume_atomic_pointer) == 4,
      "atomic returned function pointer remains distinct");

  assert(plain_consumer(plain_factory, 40) == 42);
  assert(const_consumer(const_pointer_factory, 40) == 42);
  assert(volatile_consumer(volatile_pointer_factory, 40) == 42);
  assert(atomic_consumer(atomic_pointer_factory, 40) == 42);
  return 0;
}
