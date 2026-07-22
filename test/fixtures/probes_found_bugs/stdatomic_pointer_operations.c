#include <assert.h>
#include <stdatomic.h>

static int object_evaluations;
static int delta_evaluations;

static _Atomic(int *) *selected_pointer(_Atomic(int *) *object) {
  object_evaluations++;
  return object;
}

static int selected_delta(void) {
  delta_evaluations++;
  return 2;
}

int main(void) {
  int values[8] = {0};
  _Atomic(int *) pointer;
  atomic_init(&pointer, values);
  assert(atomic_load(&pointer) == values);

  atomic_store(&pointer, values + 1);
  assert(atomic_load_explicit(&pointer, memory_order_relaxed) == values + 1);

  int *old = atomic_exchange(&pointer, values + 3);
  assert(old == values + 1);
  assert(atomic_load(&pointer) == values + 3);

  int *expected = values + 3;
  assert(atomic_compare_exchange_strong(
      &pointer, &expected, values + 5));
  assert(expected == values + 3);
  assert(atomic_load(&pointer) == values + 5);

  expected = values;
  assert(!atomic_compare_exchange_weak_explicit(
      &pointer, &expected, values + 7,
      memory_order_seq_cst, memory_order_relaxed));
  assert(expected == values + 5);
  assert(atomic_load(&pointer) == values + 5);

  old = atomic_fetch_sub(&pointer, 2);
  assert(old == values + 5);
  assert(atomic_load(&pointer) == values + 3);

  old = atomic_fetch_add_explicit(
      selected_pointer(&pointer), selected_delta(),
      memory_order_acq_rel);
  assert(old == values + 3);
  assert(atomic_load(&pointer) == values + 5);
  assert(object_evaluations == 1);
  assert(delta_evaluations == 1);
  return 0;
}
