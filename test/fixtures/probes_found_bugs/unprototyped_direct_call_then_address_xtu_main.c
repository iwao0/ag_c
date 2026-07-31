// A direct unprototyped call may establish the promoted argument ABI before
// the same unresolved function address is materialized later in the body.
// Expected with the companion TU: exit=42.

enum signed_state {
  SIGNED_STATE_NEGATIVE = -1,
  SIGNED_STATE_VALUE = 17
};

enum unsigned_state {
  UNSIGNED_STATE_ZERO = 0,
  UNSIGNED_STATE_VALUE = 25
};

int sum_atomic_enum_values_reverse();

static _Atomic(enum signed_state) signed_value =
    SIGNED_STATE_VALUE;
static _Atomic(enum unsigned_state) unsigned_value =
    UNSIGNED_STATE_VALUE;

int main(void) {
  int result =
      sum_atomic_enum_values_reverse(signed_value, unsigned_value);
  int (*taken_address)() = sum_atomic_enum_values_reverse;
  if (taken_address == 0)
    return 1;
  return result;
}
