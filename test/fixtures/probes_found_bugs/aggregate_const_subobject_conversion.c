#include <assert.h>

struct ImmutableValue {
  const int value;
};

struct PointerToConst {
  const int *pointer;
};

struct PointerToImmutable {
  struct ImmutableValue *pointer;
};

static struct ImmutableValue identity(struct ImmutableValue value) {
  return value;
}

static int consume(struct ImmutableValue value) {
  return value.value;
}

int main(void) {
  struct ImmutableValue source = {7};
  struct ImmutableValue initialized = source;
  struct ImmutableValue returned = identity(source);
  assert(initialized.value == 7);
  assert(returned.value == 7);
  assert(consume(source) == 7);

  int first = 1;
  int second = 2;
  struct PointerToConst pointer_left = {&first};
  struct PointerToConst pointer_right = {&second};
  pointer_left = pointer_right;
  assert(*pointer_left.pointer == 2);

  struct ImmutableValue first_value = {3};
  struct ImmutableValue second_value = {4};
  struct PointerToImmutable aggregate_left = {&first_value};
  struct PointerToImmutable aggregate_right = {&second_value};
  aggregate_left = aggregate_right;
  assert(aggregate_left.pointer->value == 4);
  return 0;
}
