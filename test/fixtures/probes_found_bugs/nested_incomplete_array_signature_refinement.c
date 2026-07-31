#include <assert.h>

#define IS_TYPE(expression, type) \
  _Generic((expression), type: 1, default: 0)

typedef int incomplete_cube[][3];
typedef int complete_cube[2][3];

typedef int incomplete_sum_function(incomplete_cube *cube);
typedef int complete_sum_function(complete_cube *cube);
typedef incomplete_cube *incomplete_factory_function(void);
typedef complete_cube *complete_factory_function(void);
typedef int incomplete_consumer_function(
    incomplete_factory_function *factory);
typedef int complete_consumer_function(
    complete_factory_function *factory);

static int values[2][3] = {
    {1, 2, 3},
    {10, 11, 15},
};

int sum_cube(incomplete_cube *cube);
static incomplete_sum_function *saved_sum = sum_cube;

incomplete_cube *get_cube(void);
static incomplete_factory_function *saved_get = get_cube;

int consume_factory(incomplete_factory_function *factory);
static incomplete_consumer_function *saved_consumer = consume_factory;

int sum_cube(complete_cube *cube) {
  int total = 0;
  for (int row = 0; row < 2; ++row) {
    for (int column = 0; column < 3; ++column) {
      total += (*cube)[row][column];
    }
  }
  return total;
}

complete_cube *get_cube(void) {
  return &values;
}

int consume_factory(complete_factory_function *factory) {
  return sum_cube(factory());
}

int main(void) {
  complete_sum_function *complete_sum = sum_cube;
  complete_factory_function *complete_get = get_cube;
  complete_consumer_function *complete_consumer = consume_factory;

  _Static_assert(
      IS_TYPE(1 ? saved_sum : complete_sum, complete_sum_function *),
      "conditional composite function type completes the nested array bound");
  _Static_assert(
      IS_TYPE(1 ? saved_get : complete_get, complete_factory_function *),
      "conditional composite return type completes the nested array bound");
  _Static_assert(
      sizeof *(1 ? saved_get : complete_get)() == sizeof values,
      "composite return type retains the completed multidimensional shape");
  _Static_assert(
      IS_TYPE(1 ? saved_consumer : complete_consumer,
              complete_consumer_function *),
      "nested callback return type completes the multidimensional bound");

  assert(saved_sum(&values) == 42);
  assert(complete_sum(&values) == 42);
  assert((*saved_get())[1][2] == 15);
  assert((*complete_get())[0][2] == 3);
  assert((1 ? saved_sum : complete_sum)(&values) == 42);
  assert((*(1 ? saved_get : complete_get)())[1][1] == 11);
  assert(saved_consumer(get_cube) == 42);
  assert(complete_consumer(get_cube) == 42);
  assert((1 ? saved_consumer : complete_consumer)(get_cube) == 42);
  return 0;
}
