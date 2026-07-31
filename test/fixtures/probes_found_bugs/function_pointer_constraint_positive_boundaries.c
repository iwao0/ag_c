#include <assert.h>

#define IS_TYPE(expression, type) \
  _Generic((expression), type: 1, default: 0)

typedef int unary_function(int);
typedef int qualified_parameter_function(const int);
typedef int (*unary_pointer)(int);

typedef int array_parameter_function(int values[const static 1]);
typedef int pointer_parameter_function(int *values);

typedef int callback_parameter_function(unary_function callback, int value);
typedef int callback_pointer_parameter_function(unary_pointer callback,
                                                int value);

struct box {
  int value;
};

static int add_one(const int value) {
  return value + 1;
}

static int subtract_one(int value) {
  return value - 1;
}

static int read_first(int *values) {
  return values[0];
}

static int invoke(unary_function callback, int value) {
  return callback(value);
}

static int invoke_array(array_parameter_function callback, int *values) {
  return callback(values);
}

static unary_pointer choose_callback(int choose_add) {
  return choose_add ? add_one : subtract_one;
}

static void *as_void_pointer(struct box *value) {
  return value;
}

static struct box *as_box_pointer(void *value) {
  return value;
}

static const void *choose_object_pointer(int choose_typed,
                                         const struct box *typed,
                                         void *generic) {
  return choose_typed ? typed : generic;
}

int main(void) {
  _Static_assert(
      IS_TYPE((unary_pointer)0, qualified_parameter_function *),
      "top-level parameter qualifiers do not change function compatibility");
  _Static_assert(
      IS_TYPE((array_parameter_function *)0, pointer_parameter_function *),
      "array parameters adjust to compatible pointer parameters");
  _Static_assert(
      IS_TYPE((callback_parameter_function *)0,
              callback_pointer_parameter_function *),
      "function parameters adjust to compatible function pointers");

  unary_pointer assigned = add_one;
  unary_function *addressed = &add_one;
  unary_pointer conditional = 0 ? add_one : subtract_one;

  assert(assigned == addressed);
  assert(assigned != subtract_one);
  assert(conditional == subtract_one);
  assert(invoke(assigned, 41) == 42);
  assert(choose_callback(1)(9) == 10);
  assert(choose_callback(0)(9) == 8);

  int values[2] = {42, 9};
  array_parameter_function *array_callback = read_first;
  callback_parameter_function *callback_invoker = invoke;
  assert(invoke_array(array_callback, values) == 42);
  assert(callback_invoker(add_one, 41) == 42);

  unary_pointer null_callback = 0;
  assert(null_callback == 0);
  null_callback = 1 ? add_one : 0;
  assert(null_callback == add_one);

  struct box value = {42};
  void *generic = as_void_pointer(&value);
  struct box *roundtrip = as_box_pointer(generic);
  const struct box *qualified = &value;
  const void *selected_typed =
      choose_object_pointer(1, qualified, generic);
  const void *selected_generic =
      choose_object_pointer(0, qualified, generic);

  assert(roundtrip == &value);
  assert(generic == &value);
  assert(selected_typed == qualified);
  assert(selected_generic == generic);
  assert(((const struct box *)selected_typed)->value == 42);
  return 0;
}
