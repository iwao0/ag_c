#include <assert.h>

struct pair {
  int first;
  int second;
};

static int increment(int value) {
  return value + 1;
}

static int read_const(const int *value) {
  return *value;
}

static int read_int(int *value) {
  return *value;
}

static int read_void(void *value) {
  return *(int *)value;
}

static int call_callback(int (*callback)(int), int value) {
  return callback(value);
}

static double half(double value) {
  return value / 2.0;
}

static struct pair copy_pair(struct pair value) {
  return value;
}

static int top_level_const(const int value) {
  return value;
}

static int sum_array(int values[2]) {
  return values[0] + values[1];
}

static const int *return_const_pointer(const int *value) {
  return value;
}

static void *return_void_pointer(int *value) {
  return value;
}

static int *return_typed_pointer(void *value) {
  return value;
}

static int *return_null_pointer(void) {
  return 0;
}

int main(void) {
  int value = 8;
  int values[2] = {3, 4};
  struct pair pair = {5, 6};
  struct pair copied;

  assert(read_const(&value) == 8);
  assert(read_int((void *)&value) == 8);
  assert(read_void(&value) == 8);
  assert((0 ? read_const(0) : value) == 8); /* Type-check null without dereferencing it. */
  assert(call_callback(increment, 9) == 10);
  assert(half(5) == 2.5);
  copied = copy_pair(pair);
  assert(copied.first == 5 && copied.second == 6);
  assert(top_level_const(value) == 8);
  assert(sum_array(values) == 7);
  assert(*return_const_pointer(&value) == 8);
  assert(*(int *)return_void_pointer(&value) == 8);
  assert(*return_typed_pointer((void *)&value) == 8);
  assert(return_null_pointer() == 0);
  return 0;
}
