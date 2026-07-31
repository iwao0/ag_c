// C11 stdatomic generic operations preserve Atomic enum identity, compatible
// integer representation, expected-value updates, and single evaluation.
// Expected: exit=0.

#include <limits.h>
#include <stdatomic.h>

enum unsigned_state {
  UNSIGNED_STATE_ZERO = 0,
  UNSIGNED_STATE_ONE = 1,
  UNSIGNED_STATE_FIVE = 5,
  UNSIGNED_STATE_TWENTY_FIVE = 25
};

enum signed_state {
  SIGNED_STATE_NEGATIVE = -17,
  SIGNED_STATE_ZERO = 0,
  SIGNED_STATE_FIVE = 5,
  SIGNED_STATE_SEVENTEEN = 17
};

#define HAS_UNSIGNED_ENUM_TYPE(expression) \
  _Generic((expression), enum unsigned_state: 1, default: 0)
#define HAS_SIGNED_ENUM_TYPE(expression) \
  _Generic((expression), enum signed_state: 1, default: 0)
#define HAS_BOOL_TYPE(expression) \
  _Generic((expression), _Bool: 1, default: 0)

struct holder {
  _Atomic(enum unsigned_state) unsigned_member;
  volatile _Atomic(enum signed_state) signed_member;
};

static _Atomic(enum unsigned_state) unsigned_value =
    UNSIGNED_STATE_FIVE;
static volatile _Atomic(enum signed_state) signed_value =
    SIGNED_STATE_NEGATIVE;
static struct holder global_holder = {
    UNSIGNED_STATE_ONE, SIGNED_STATE_FIVE};

static int object_evaluations;
static int expected_evaluations;
static int value_evaluations;
static int order_evaluations;

static _Atomic(enum unsigned_state) *select_unsigned(void) {
  object_evaluations++;
  return &unsigned_value;
}

static volatile _Atomic(enum signed_state) *select_signed(void) {
  object_evaluations++;
  return &signed_value;
}

static enum unsigned_state *select_unsigned_expected(
    enum unsigned_state *expected) {
  expected_evaluations++;
  return expected;
}

static enum signed_state *select_signed_expected(
    enum signed_state *expected) {
  expected_evaluations++;
  return expected;
}

static enum unsigned_state next_unsigned(int value) {
  value_evaluations++;
  return (enum unsigned_state)value;
}

static enum signed_state next_signed(int value) {
  value_evaluations++;
  return (enum signed_state)value;
}

static memory_order next_order(void) {
  order_evaluations++;
  return memory_order_seq_cst;
}

int main(void) {
  _Atomic(enum unsigned_state) local_unsigned;
  volatile _Atomic(enum signed_state) local_signed;
  enum unsigned_state unsigned_expected = UNSIGNED_STATE_ONE;
  enum signed_state signed_expected = SIGNED_STATE_FIVE;
  enum unsigned_state old_unsigned;
  enum signed_state old_signed;

  if (!HAS_UNSIGNED_ENUM_TYPE(atomic_load(select_unsigned())) ||
      !HAS_SIGNED_ENUM_TYPE(
          atomic_load_explicit(select_signed(), next_order())) ||
      !HAS_UNSIGNED_ENUM_TYPE(
          atomic_exchange(select_unsigned(), next_unsigned(0))) ||
      !HAS_SIGNED_ENUM_TYPE(atomic_exchange_explicit(
          select_signed(), next_signed(0), next_order())) ||
      !HAS_BOOL_TYPE(atomic_compare_exchange_strong(
          select_unsigned(),
          select_unsigned_expected(&unsigned_expected),
          next_unsigned(0))) ||
      object_evaluations != 0 || expected_evaluations != 0 ||
      value_evaluations != 0 || order_evaluations != 0)
    return 1;

  atomic_init(&local_unsigned, UNSIGNED_STATE_ONE);
  atomic_init(&local_signed, SIGNED_STATE_NEGATIVE);
  if (atomic_load(&local_unsigned) != UNSIGNED_STATE_ONE ||
      atomic_load(&local_signed) != SIGNED_STATE_NEGATIVE)
    return 2;

  if (atomic_load_explicit(select_unsigned(), next_order()) !=
          UNSIGNED_STATE_FIVE ||
      atomic_load(select_signed()) != SIGNED_STATE_NEGATIVE ||
      object_evaluations != 2 || order_evaluations != 1)
    return 3;

  atomic_store_explicit(
      select_unsigned(), next_unsigned(25), next_order());
  atomic_store(select_signed(), next_signed(5));
  if (atomic_load(&unsigned_value) != UNSIGNED_STATE_TWENTY_FIVE ||
      atomic_load(&signed_value) != SIGNED_STATE_FIVE ||
      object_evaluations != 4 || value_evaluations != 2 ||
      order_evaluations != 2)
    return 4;

  old_unsigned = atomic_exchange(
      select_unsigned(), next_unsigned(1));
  old_signed = atomic_exchange_explicit(
      select_signed(), next_signed(17), next_order());
  if (old_unsigned != UNSIGNED_STATE_TWENTY_FIVE ||
      atomic_load(&unsigned_value) != UNSIGNED_STATE_ONE ||
      old_signed != SIGNED_STATE_FIVE ||
      atomic_load(&signed_value) != SIGNED_STATE_SEVENTEEN ||
      object_evaluations != 6 || value_evaluations != 4 ||
      order_evaluations != 3)
    return 5;

  if (!atomic_compare_exchange_strong(
          select_unsigned(),
          select_unsigned_expected(&unsigned_expected),
          next_unsigned(5)) ||
      unsigned_expected != UNSIGNED_STATE_ONE ||
      atomic_load(&unsigned_value) != UNSIGNED_STATE_FIVE ||
      object_evaluations != 7 || expected_evaluations != 1 ||
      value_evaluations != 5)
    return 6;

  if (atomic_compare_exchange_weak_explicit(
          select_signed(),
          select_signed_expected(&signed_expected),
          next_signed(-17), next_order(), next_order()) ||
      signed_expected != SIGNED_STATE_SEVENTEEN ||
      atomic_load(&signed_value) != SIGNED_STATE_SEVENTEEN ||
      object_evaluations != 8 || expected_evaluations != 2 ||
      value_evaluations != 6 || order_evaluations != 5)
    return 7;

  if (!atomic_compare_exchange_weak(
          select_signed(),
          select_signed_expected(&signed_expected),
          next_signed(-17)) ||
      signed_expected != SIGNED_STATE_SEVENTEEN ||
      atomic_load(&signed_value) != SIGNED_STATE_NEGATIVE ||
      object_evaluations != 9 || expected_evaluations != 3 ||
      value_evaluations != 7)
    return 8;

  atomic_store(
      &unsigned_value, (enum unsigned_state)-1);
  if ((unsigned int)atomic_load(&unsigned_value) != UINT_MAX)
    return 9;

  atomic_store(
      &global_holder.unsigned_member,
      UNSIGNED_STATE_TWENTY_FIVE);
  old_signed = atomic_exchange(
      &global_holder.signed_member, SIGNED_STATE_SEVENTEEN);
  if (atomic_load(&global_holder.unsigned_member) !=
          UNSIGNED_STATE_TWENTY_FIVE ||
      old_signed != SIGNED_STATE_FIVE ||
      atomic_load(&global_holder.signed_member) !=
          SIGNED_STATE_SEVENTEEN)
    return 10;

  return 0;
}
