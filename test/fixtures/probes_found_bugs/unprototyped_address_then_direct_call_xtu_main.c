// An unprototyped function address may be materialized before a direct call
// supplies the promoted argument ABI used by the companion definition.
// Expected with the companion TU: exit=42.

enum signed_state {
  SIGNED_STATE_NEGATIVE = -1,
  SIGNED_STATE_VALUE = 17
};

enum unsigned_state {
  UNSIGNED_STATE_ZERO = 0,
  UNSIGNED_STATE_VALUE = 25
};

int sum_atomic_enum_values();

static int (*taken_address)() = sum_atomic_enum_values;
static _Atomic(enum signed_state) signed_value =
    SIGNED_STATE_VALUE;
static _Atomic(enum unsigned_state) unsigned_value =
    UNSIGNED_STATE_VALUE;

int main(void) {
  if (taken_address == 0)
    return 1;
  return sum_atomic_enum_values(signed_value, unsigned_value);
}
