#include <assert.h>

#define IS_TYPE(expression, type) \
  _Generic((expression), type: 1, default: 0)

typedef int array_callback_function(int values[const static 2]);
typedef int pointer_callback_function(int *values);

typedef int unary_function(int value);
typedef int function_parameter_callback_function(
    unary_function transform, int value);
typedef int function_pointer_callback_function(
    unary_function *transform, int value);

typedef int array_consumer_function(
    array_callback_function *callback, int *values);
typedef int pointer_consumer_function(
    pointer_callback_function *callback, int *values);
typedef int function_parameter_consumer_function(
    function_parameter_callback_function *callback,
    unary_function *transform, int value);
typedef int function_pointer_consumer_function(
    function_pointer_callback_function *callback,
    unary_function *transform, int value);

static int sum_pair(int values[const static 2]) {
  return values[0] + values[1];
}

static int add_three(int value) {
  return value + 3;
}

static int apply_transform(unary_function transform, int value) {
  return transform(value);
}

int consume_array_callback(
    array_callback_function *callback, int *values);
static array_consumer_function *saved_array_consumer =
    consume_array_callback;

int consume_function_callback(
    function_parameter_callback_function *callback,
    unary_function *transform, int value);
static function_parameter_consumer_function *saved_function_consumer =
    consume_function_callback;

int consume_array_callback(
    pointer_callback_function *callback, int *values) {
  return callback(values);
}

int consume_function_callback(
    function_pointer_callback_function *callback,
    unary_function *transform, int value) {
  return callback(transform, value);
}

int main(void) {
  int values[2] = {19, 23};
  pointer_consumer_function *pointer_consumer =
      consume_array_callback;
  function_pointer_consumer_function *function_pointer_consumer =
      consume_function_callback;

  _Static_assert(
      IS_TYPE(1 ? saved_array_consumer : pointer_consumer,
              pointer_consumer_function *),
      "nested array parameter adjusts to a pointer");
  _Static_assert(
      IS_TYPE(1 ? saved_function_consumer : function_pointer_consumer,
              function_pointer_consumer_function *),
      "nested function parameter adjusts to a function pointer");

  assert(saved_array_consumer(sum_pair, values) == 42);
  assert(pointer_consumer(sum_pair, values) == 42);
  assert(saved_function_consumer(apply_transform, add_three, 39) == 42);
  assert(function_pointer_consumer(
             apply_transform, add_three, 39) == 42);
  return 0;
}
