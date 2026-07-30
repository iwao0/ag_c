/*
 * A discarded-value expression that designates a volatile scalar still
 * performs the volatile read.  Its result may be unused, but the access is
 * observable and must survive dead-code elimination.
 */
#include <assert.h>

struct volatile_fields {
  volatile unsigned int bits : 3;
  volatile double real;
};

static volatile int global_value = 7;
static int selector_calls;

static volatile int *select_value(volatile int *pointer) {
  selector_calls++;
  return pointer;
}

int main(void) {
  volatile int local = 11;
  volatile int *pointer = &local;
  int plain = 13;
  int * volatile volatile_pointer = &plain;
  volatile short values[2] = {17, 19};
  struct volatile_fields fields = {5, 23.5};

  (void)global_value;
  (void)local;
  (void)*pointer;
  (void)volatile_pointer;
  (void)values[1];
  (void)fields.bits;
  (void)fields.real;

  selector_calls = 0;
  (void)*select_value(&global_value);
  assert(selector_calls == 1);

  assert(global_value == 7);
  assert(local == 11);
  assert(*pointer == 11);
  assert(volatile_pointer == &plain);
  assert(values[0] == 17 && values[1] == 19);
  assert(fields.bits == 5);
  assert(fields.real == 23.5);
  return 0;
}
