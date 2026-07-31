// atomic_compare_exchange accepts a pointer to the integer type compatible
// with the Atomic enum, while preserving expected-value updates and evaluating
// each macro argument exactly once.
// Expected: exit=0.

#include <stdatomic.h>

enum positive_state {
  POSITIVE_ZERO = 0,
  POSITIVE_ONE = 1,
  POSITIVE_FIVE = 5
};

enum negative_state {
  NEGATIVE_ONE = -1,
  NEGATIVE_ZERO = 0,
  NEGATIVE_SEVEN = 7
};

typedef unsigned int positive_compatible_t;
typedef int negative_compatible_t;

static _Atomic(enum positive_state) positive_value = POSITIVE_ONE;
static volatile _Atomic(enum negative_state) negative_value = NEGATIVE_ONE;

static int object_evaluations;
static int expected_evaluations;
static int desired_evaluations;
static int order_evaluations;

static _Atomic(enum positive_state) *select_positive_object(void) {
  object_evaluations++;
  return &positive_value;
}

static volatile _Atomic(enum negative_state) *select_negative_object(void) {
  object_evaluations++;
  return &negative_value;
}

static positive_compatible_t *select_unsigned_expected(
    positive_compatible_t *expected) {
  expected_evaluations++;
  return expected;
}

static negative_compatible_t *select_signed_expected(
    negative_compatible_t *expected) {
  expected_evaluations++;
  return expected;
}

static enum positive_state next_positive(enum positive_state desired) {
  desired_evaluations++;
  return desired;
}

static enum negative_state next_negative(enum negative_state desired) {
  desired_evaluations++;
  return desired;
}

static memory_order next_order(void) {
  order_evaluations++;
  return memory_order_seq_cst;
}

int main(void) {
  positive_compatible_t unsigned_expected = POSITIVE_ONE;
  negative_compatible_t signed_expected = NEGATIVE_ONE;
  positive_compatible_t unsigned_expected_array[1];
  positive_compatible_t *restrict unsigned_expected_pointer =
      unsigned_expected_array;
  int expected_vla_length = 1;
  negative_compatible_t signed_expected_vla[expected_vla_length];

  if (!atomic_compare_exchange_strong(
          select_positive_object(),
          select_unsigned_expected(&unsigned_expected),
          next_positive(POSITIVE_FIVE)) ||
      unsigned_expected != POSITIVE_ONE ||
      atomic_load(&positive_value) != POSITIVE_FIVE ||
      object_evaluations != 1 || expected_evaluations != 1 ||
      desired_evaluations != 1 || order_evaluations != 0)
    return 1;

  unsigned_expected = POSITIVE_ZERO;
  if (atomic_compare_exchange_weak_explicit(
          select_positive_object(),
          select_unsigned_expected(&unsigned_expected),
          next_positive(POSITIVE_ONE),
          next_order(), next_order()) ||
      unsigned_expected != POSITIVE_FIVE ||
      atomic_load(&positive_value) != POSITIVE_FIVE ||
      object_evaluations != 2 || expected_evaluations != 2 ||
      desired_evaluations != 2 || order_evaluations != 2)
    return 2;

  if (!atomic_compare_exchange_weak(
          select_negative_object(),
          select_signed_expected(&signed_expected),
          next_negative(NEGATIVE_SEVEN)) ||
      signed_expected != NEGATIVE_ONE ||
      atomic_load(&negative_value) != NEGATIVE_SEVEN ||
      object_evaluations != 3 || expected_evaluations != 3 ||
      desired_evaluations != 3 || order_evaluations != 2)
    return 3;

  signed_expected = NEGATIVE_ONE;
  if (atomic_compare_exchange_strong_explicit(
          select_negative_object(),
          select_signed_expected(&signed_expected),
          next_negative(NEGATIVE_ZERO),
          next_order(), next_order()) ||
      signed_expected != NEGATIVE_SEVEN ||
      atomic_load(&negative_value) != NEGATIVE_SEVEN ||
      object_evaluations != 4 || expected_evaluations != 4 ||
      desired_evaluations != 4 || order_evaluations != 4)
    return 4;

  unsigned_expected_array[0] = POSITIVE_FIVE;
  if (!atomic_compare_exchange_strong(
          select_positive_object(), unsigned_expected_array,
          next_positive(POSITIVE_ZERO)) ||
      unsigned_expected_array[0] != POSITIVE_FIVE ||
      atomic_load(&positive_value) != POSITIVE_ZERO ||
      object_evaluations != 5 || expected_evaluations != 4 ||
      desired_evaluations != 5 || order_evaluations != 4)
    return 5;

  unsigned_expected_array[0] = POSITIVE_ONE;
  if (atomic_compare_exchange_weak_explicit(
          select_positive_object(), unsigned_expected_pointer,
          next_positive(POSITIVE_FIVE),
          next_order(), next_order()) ||
      unsigned_expected_array[0] != POSITIVE_ZERO ||
      atomic_load(&positive_value) != POSITIVE_ZERO ||
      object_evaluations != 6 || expected_evaluations != 4 ||
      desired_evaluations != 6 || order_evaluations != 6)
    return 6;

  signed_expected_vla[0] = NEGATIVE_SEVEN;
  if (!atomic_compare_exchange_strong(
          select_negative_object(), signed_expected_vla,
          next_negative(NEGATIVE_ONE)) ||
      signed_expected_vla[0] != NEGATIVE_SEVEN ||
      atomic_load(&negative_value) != NEGATIVE_ONE ||
      object_evaluations != 7 || expected_evaluations != 4 ||
      desired_evaluations != 7 || order_evaluations != 6)
    return 7;

  return 0;
}
