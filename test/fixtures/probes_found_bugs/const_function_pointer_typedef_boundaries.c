/*
 * A typedef may make the pointer object itself const while leaving the
 * pointed-to function type unchanged.  Calls and lvalue conversion use the
 * ordinary function-pointer value, but taking the object's address must keep
 * the top-level const qualification.
 */
#include <assert.h>

typedef int (*Unary)(int);
typedef int (*const ConstUnary)(int);

static int add_one(int value) { return value + 1; }
static int triple(int value) { return value * 3; }
static int subtract_two(int value) { return value - 2; }

static ConstUnary global_direct = add_one;
static ConstUnary global_table[2] = {triple, subtract_two};

struct Dispatch {
  ConstUnary primary;
  ConstUnary fallback[2];
};

static struct Dispatch global_dispatch = {
    add_one,
    {triple, subtract_two},
};

_Static_assert(sizeof(ConstUnary) == sizeof(Unary),
               "qualification does not change pointer representation");
_Static_assert(_Generic((ConstUnary)add_one, Unary: 1, default: 0),
               "a converted scalar value has no top-level qualifier");

static int invoke_parameter(ConstUnary operation, int value) {
  return operation(value);
}

static ConstUnary *static_slot(void) {
  static ConstUnary operation = subtract_two;
  return &operation;
}

int main(void) {
  ConstUnary local = triple;
  ConstUnary local_table[2] = {add_one, subtract_two};
  ConstUnary *local_slot = &local;
  ConstUnary *table_slot = &local_table[1];
  struct Dispatch copy = global_dispatch;
  Unary selected = 1 ? local : global_direct;
  Unary other = 0 ? local : global_table[1];

  assert(global_direct(10) == 11);
  assert(global_table[0](10) == 30);
  assert(global_table[1](10) == 8);
  assert(local(7) == 21);
  assert(local_table[0](7) == 8);
  assert(local_table[1](7) == 5);
  assert((*local_slot)(6) == 18);
  assert((*table_slot)(6) == 4);
  assert((*static_slot())(9) == 7);
  assert(invoke_parameter(global_direct, 20) == 21);
  assert(invoke_parameter(local, 20) == 60);
  assert(copy.primary(4) == 5);
  assert(copy.fallback[0](4) == 12);
  assert(copy.fallback[1](4) == 2);
  assert(selected(5) == 15);
  assert(other(5) == 3);

  assert(_Generic(local, Unary: 1, default: 0));
  assert(_Generic(global_table[0], Unary: 1, default: 0));
  assert(_Generic(&local, ConstUnary *: 1, default: 0));
  assert(_Generic(&global_direct, ConstUnary *: 1, default: 0));
  assert(_Generic(&global_table[0], ConstUnary *: 1, default: 0));
  assert(global_direct == add_one);
  assert(local == triple);
  assert(&global_table[1] - &global_table[0] == 1);
  return 0;
}
