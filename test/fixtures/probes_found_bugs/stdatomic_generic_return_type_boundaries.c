// stdatomic generic-function _Bool and void result types.
// Expected: exit=0
#include <stdatomic.h>

int main(void) {
  atomic_int value = ATOMIC_VAR_INIT(1);
  int expected = 1;

  if (!atomic_compare_exchange_strong(
          &value, &expected, 2) ||
      value != 2 || expected != 1)
    return 1;
  expected = 0;
  if (atomic_compare_exchange_weak(
          &value, &expected, 3) ||
      value != 2 || expected != 2)
    return 2;

  expected = 2;
  if (!atomic_compare_exchange_strong_explicit(
          &value, &expected, 4,
          memory_order_acq_rel, memory_order_acquire) ||
      value != 4 || expected != 2)
    return 3;
  expected = 0;
  if (atomic_compare_exchange_weak_explicit(
          &value, &expected, 5,
          memory_order_acq_rel, memory_order_acquire) ||
      value != 4 || expected != 4)
    return 4;

  atomic_flag flag = ATOMIC_FLAG_INIT;
  if (atomic_flag_test_and_set(&flag) ||
      !atomic_flag_test_and_set_explicit(
          &flag, memory_order_acquire))
    return 5;
  atomic_flag_clear_explicit(
      &flag, memory_order_release);
  if (atomic_flag_test_and_set(&flag))
    return 6;
  atomic_flag_clear(&flag);

  atomic_thread_fence(memory_order_seq_cst);
  atomic_signal_fence(memory_order_seq_cst);

  if (!_Generic(
          atomic_compare_exchange_strong(
              &value, &expected, 0),
          _Bool: 1,
          default: 0) ||
      !_Generic(
          atomic_compare_exchange_weak(
              &value, &expected, 0),
          _Bool: 1,
          default: 0) ||
      !_Generic(
          atomic_compare_exchange_strong_explicit(
              &value, &expected, 0,
              memory_order_relaxed, memory_order_relaxed),
          _Bool: 1,
          default: 0) ||
      !_Generic(
          atomic_compare_exchange_weak_explicit(
              &value, &expected, 0,
              memory_order_relaxed, memory_order_relaxed),
          _Bool: 1,
          default: 0) ||
      !_Generic(
          atomic_flag_test_and_set(&flag),
          _Bool: 1,
          default: 0) ||
      !_Generic(
          atomic_flag_test_and_set_explicit(
              &flag, memory_order_relaxed),
          _Bool: 1,
          default: 0))
    return 7;

  if (!_Generic(
          atomic_init(&value, 0),
          int: 0,
          default: 1) ||
      !_Generic(
          atomic_store(&value, 0),
          int: 0,
          default: 1) ||
      !_Generic(
          atomic_store_explicit(
              &value, 0, memory_order_relaxed),
          int: 0,
          default: 1) ||
      !_Generic(
          atomic_flag_clear(&flag),
          int: 0,
          default: 1) ||
      !_Generic(
          atomic_flag_clear_explicit(
              &flag, memory_order_relaxed),
          int: 0,
          default: 1) ||
      !_Generic(
          atomic_thread_fence(memory_order_seq_cst),
          int: 0,
          default: 1) ||
      !_Generic(
          atomic_signal_fence(memory_order_seq_cst),
          int: 0,
          default: 1))
    return 8;
  return 0;
}
