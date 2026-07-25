// stdatomic generic-function const/volatile object qualification boundaries.
// Expected: exit=0
#include <stdatomic.h>

struct pair {
  int x;
  int y;
};

static const atomic_int const_value = ATOMIC_VAR_INIT(7);
static const volatile atomic_int const_volatile_value =
    ATOMIC_VAR_INIT(11);
static const _Atomic(struct pair) const_pair =
    (struct pair){13, 17};
static volatile atomic_int volatile_value = ATOMIC_VAR_INIT(19);
static int object_evaluations;
static int order_evaluations;

static const atomic_int *selected_const_value(void) {
  object_evaluations++;
  return &const_value;
}

static memory_order selected_order(void) {
  order_evaluations++;
  return memory_order_relaxed;
}

int main(void) {
  int loaded = atomic_load(&const_value);
  int loaded_const_volatile = atomic_load(&const_volatile_value);
  struct pair pair = atomic_load(&const_pair);
  if (loaded != 7 || loaded_const_volatile != 11 ||
      pair.x != 13 || pair.y != 17)
    return 1;

  loaded = atomic_load_explicit(
      selected_const_value(), selected_order());
  if (loaded != 7 || object_evaluations != 1 ||
      order_evaluations != 1)
    return 2;

  atomic_store(&volatile_value, 23);
  if (atomic_exchange(&volatile_value, 29) != 23)
    return 3;
  int expected = 29;
  if (!atomic_compare_exchange_strong(
          &volatile_value, &expected, 31))
    return 4;
  if (atomic_fetch_add(&volatile_value, 5) != 31 ||
      atomic_load(&volatile_value) != 36)
    return 5;

  if (!_Generic(atomic_load(&const_value), int: 1, default: 0) ||
      !_Generic(
          atomic_load(&const_pair),
          struct pair: 1,
          default: 0))
    return 6;
  return 0;
}
