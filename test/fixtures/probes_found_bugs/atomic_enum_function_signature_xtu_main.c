// An enumeration and its compatible integer type remain compatible inside
// atomic pointer and pointer-to-atomic function signature positions.
// Expected with the companion TU: exit=42.

enum atomic_signed_result {
  ATOMIC_SIGNED_NEGATIVE = -1,
  ATOMIC_SIGNED_VALUE = 17
};

enum atomic_unsigned_result {
  ATOMIC_UNSIGNED_ZERO = 0,
  ATOMIC_UNSIGNED_VALUE = 25
};

_Atomic(enum atomic_signed_result *)
roundtrip_atomic_enum_pointer(
    _Atomic(enum atomic_signed_result *) value);

int read_atomic_enum_pointee(
    _Atomic(enum atomic_unsigned_result) *value);

static enum atomic_signed_result signed_result =
    ATOMIC_SIGNED_VALUE;
static _Atomic(enum atomic_unsigned_result) unsigned_result =
    ATOMIC_UNSIGNED_VALUE;

int main(void) {
  _Atomic(enum atomic_signed_result *) input = &signed_result;
  _Atomic(enum atomic_signed_result *) output =
      roundtrip_atomic_enum_pointer(input);
  return *output + read_atomic_enum_pointee(&unsigned_result);
}
