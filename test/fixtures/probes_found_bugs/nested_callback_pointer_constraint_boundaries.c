#include <assert.h>

#define IS_TYPE(expression, type) \
  _Generic((expression), type: 1, default: 0)

typedef int array_callback_function(int values[const static 2]);
typedef int pointer_callback_function(int *values);
typedef int array_consumer_function(
    array_callback_function *callback, int *values);
typedef int pointer_consumer_function(
    pointer_callback_function *callback, int *values);

static int sum_pair(int values[const static 2]) {
  return values[0] + values[1];
}

static int consume_pointer(
    pointer_callback_function *callback, int *values) {
  return callback(values);
}

static int apply_consumer(
    pointer_consumer_function *consumer,
    pointer_callback_function *callback, int *values) {
  return consumer(callback, values);
}

static array_consumer_function *return_consumer(void) {
  return consume_pointer;
}

int main(void) {
  int values[2] = {19, 23};
  array_consumer_function *array_consumer = consume_pointer;
  pointer_consumer_function *pointer_consumer;

  pointer_consumer = array_consumer;
  _Static_assert(
      IS_TYPE(1 ? array_consumer : pointer_consumer,
              pointer_consumer_function *),
      "conditional expression keeps the compatible nested callback type");

  assert(array_consumer == pointer_consumer);
  assert(array_consumer != 0);
  assert(array_consumer(sum_pair, values) == 42);
  assert(pointer_consumer(sum_pair, values) == 42);
  assert((1 ? array_consumer : pointer_consumer)(
             sum_pair, values) == 42);
  assert(apply_consumer(array_consumer, sum_pair, values) == 42);
  assert(return_consumer()(sum_pair, values) == 42);
  return 0;
}
