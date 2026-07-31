// Qualified Atomic enum updates preserve their integer semantics through
// globals, locals, members, arrays, pointer indirection, and selectors.
// Expected: exit=0.

#include <limits.h>

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

struct storage {
  volatile _Atomic(enum signed_state) member;
  _Atomic(enum unsigned_state) array[2];
};

static volatile _Atomic(enum unsigned_state) volatile_global =
    UNSIGNED_STATE_ONE;
static const volatile _Atomic(enum unsigned_state) readonly_global =
    UNSIGNED_STATE_FIVE;
static struct storage global_storage = {
    SIGNED_STATE_FIVE,
    {UNSIGNED_STATE_FIVE, UNSIGNED_STATE_TWENTY_FIVE}};
static _Atomic(enum unsigned_state) * volatile volatile_pointer =
    &global_storage.array[1];

static int global_selector_count;
static int readonly_selector_count;
static int member_selector_count;
static int array_selector_count;

static volatile _Atomic(enum unsigned_state) *
select_volatile_global(void) {
  global_selector_count++;
  return &volatile_global;
}

static const volatile _Atomic(enum unsigned_state) *
select_readonly_global(void) {
  readonly_selector_count++;
  return &readonly_global;
}

static volatile _Atomic(enum signed_state) *
select_member(void) {
  member_selector_count++;
  return &global_storage.member;
}

static _Atomic(enum unsigned_state) *
select_array_element(int index) {
  array_selector_count++;
  return &global_storage.array[index];
}

static enum signed_state update_parameter(
    volatile _Atomic(enum signed_state) *value) {
  return *value |= 1;
}

int main(void) {
  volatile _Atomic(enum signed_state) local =
      SIGNED_STATE_SEVENTEEN;
  enum unsigned_state unsigned_result;
  enum signed_state signed_result;
  unsigned int readonly_snapshot = *select_readonly_global();

  if (readonly_snapshot != 5U ||
      ~readonly_global != UINT_MAX - 5U ||
      readonly_selector_count != 1)
    return 1;

  unsigned_result = (*select_volatile_global()) <<= 4;
  if ((unsigned int)unsigned_result != 16U ||
      (unsigned int)volatile_global != 16U ||
      global_selector_count != 1)
    return 2;

  signed_result = (*select_member()) ^= 3;
  if ((int)signed_result != 6 ||
      (int)global_storage.member != 6 ||
      member_selector_count != 1)
    return 3;

  unsigned_result = (*select_array_element(0)) |= 8;
  if ((unsigned int)unsigned_result != 13U ||
      (unsigned int)global_storage.array[0] != 13U ||
      array_selector_count != 1)
    return 4;

  unsigned_result = (*volatile_pointer) >>= 1;
  if ((unsigned int)unsigned_result != 12U ||
      (unsigned int)global_storage.array[1] != 12U)
    return 5;

  signed_result = (local %= 5);
  if ((int)signed_result != 2 || (int)local != 2)
    return 6;
  signed_result = (local <<= 2);
  if ((int)signed_result != 8 || (int)local != 8)
    return 7;
  signed_result = update_parameter(&local);
  if ((int)signed_result != 9 || (int)local != 9)
    return 8;

  signed_result = global_storage.member++;
  if ((int)signed_result != 6 ||
      (int)global_storage.member != 7)
    return 9;
  signed_result = --global_storage.member;
  if ((int)signed_result != 6 ||
      (int)global_storage.member != 6)
    return 10;

  return 0;
}
