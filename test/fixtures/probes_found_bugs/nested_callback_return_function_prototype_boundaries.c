#include <assert.h>

#define IS_TYPE(expression, type) \
  _Generic((expression), type: 1, default: 0)

typedef int unprototyped_target_function();
typedef int prototyped_target_function(int value);
typedef unprototyped_target_function *unprototyped_factory_function(void);
typedef prototyped_target_function *prototyped_factory_function(void);
typedef int unprototyped_consumer_function(
    unprototyped_factory_function *factory, int value);
typedef int prototyped_consumer_function(
    prototyped_factory_function *factory, int value);

static int add_two(int value) {
  return value + 2;
}

static prototyped_target_function *make_callback(void) {
  return add_two;
}

int consume_factory(
    unprototyped_factory_function *factory, int value);
static unprototyped_consumer_function *saved_consumer =
    consume_factory;

int consume_factory(
    prototyped_factory_function *factory, int value) {
  return factory()(value);
}

int main(void) {
  prototyped_consumer_function *prototyped_consumer =
      consume_factory;

  _Static_assert(
      IS_TYPE(
          1 ? saved_consumer : prototyped_consumer,
          prototyped_consumer_function *),
      "nested returned function pointer refines to an int prototype");
  assert(saved_consumer(make_callback, 40) == 42);
  assert(prototyped_consumer(make_callback, 40) == 42);
  assert((1 ? saved_consumer : prototyped_consumer)(
             make_callback, 40) == 42);
  return 0;
}
