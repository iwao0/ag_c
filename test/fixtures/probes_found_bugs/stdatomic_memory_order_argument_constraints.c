// stdatomic explicit-operation memory_order argument conversions.
// Expected: exit=0
#include <complex.h>
#include <stdatomic.h>

static int order_evaluations;

static memory_order next_order(memory_order order) {
  order_evaluations++;
  return order;
}

static double next_real_order(memory_order order) {
  order_evaluations++;
  return (double)order + 0.75;
}

static double complex next_complex_order(memory_order order) {
  order_evaluations++;
  return (double)order + 9.0 * I;
}

int main(void) {
  atomic_int value = ATOMIC_VAR_INIT(1);
  atomic_flag flag = ATOMIC_FLAG_INIT;

  atomic_store_explicit(
      &value, 3, next_real_order(memory_order_release));
  if (atomic_load_explicit(
          &value, next_complex_order(memory_order_acquire)) != 3)
    return 1;
  if (atomic_exchange_explicit(
          &value, 5, next_order(memory_order_acq_rel)) != 3)
    return 2;

  int expected = 5;
  if (!atomic_compare_exchange_strong_explicit(
          &value, &expected, 7,
          next_order(memory_order_acq_rel),
          next_order(memory_order_acquire)))
    return 3;
  expected = 99;
  if (atomic_compare_exchange_weak_explicit(
          &value, &expected, 11,
          next_order(memory_order_acq_rel),
          next_order(memory_order_acquire)) ||
      expected != 7)
    return 4;

  if (atomic_fetch_add_explicit(
          &value, 2, next_order(memory_order_relaxed)) != 7 ||
      atomic_fetch_sub_explicit(
          &value, 1, next_order(memory_order_relaxed)) != 9 ||
      atomic_fetch_or_explicit(
          &value, 16, next_order(memory_order_relaxed)) != 8 ||
      atomic_fetch_xor_explicit(
          &value, 3, next_order(memory_order_relaxed)) != 24 ||
      atomic_fetch_and_explicit(
          &value, 15, next_order(memory_order_relaxed)) != 27 ||
      atomic_load(&value) != 11)
    return 5;

  if (atomic_flag_test_and_set_explicit(
          &flag, next_order(memory_order_acquire)))
    return 6;
  atomic_flag_clear_explicit(
      &flag, next_order(memory_order_release));
  atomic_thread_fence(
      next_order(memory_order_seq_cst));
  atomic_signal_fence(
      next_order(memory_order_seq_cst));

  if (order_evaluations != 16)
    return 7;
  return 0;
}
