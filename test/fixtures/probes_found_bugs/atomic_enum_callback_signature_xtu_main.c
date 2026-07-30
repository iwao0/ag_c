// Enum compatibility is preserved through an atomic function pointer whose
// callback parameter and result both use the enumeration.
// Expected with the companion TU: exit=42.

enum atomic_callback_signed_value {
  ATOMIC_CALLBACK_SIGNED_NEGATIVE = -1,
  ATOMIC_CALLBACK_SIGNED_VALUE = 42
};

#ifndef AG_C_ATOMIC_ENUM_CALLBACK_SIGNATURE_XTU_TYPES
#define AG_C_ATOMIC_ENUM_CALLBACK_SIGNATURE_XTU_TYPES
typedef enum atomic_callback_signed_value
    atomic_enum_callback_t(
        enum atomic_callback_signed_value value);
#endif

_Atomic(atomic_enum_callback_t *)
roundtrip_atomic_enum_callback(
    _Atomic(atomic_enum_callback_t *) callback);

static enum atomic_callback_signed_value
identity_atomic_enum_callback(
    enum atomic_callback_signed_value value) {
  return value;
}

int main(void) {
  _Atomic(atomic_enum_callback_t *) callback =
      identity_atomic_enum_callback;
  _Atomic(atomic_enum_callback_t *) result =
      roundtrip_atomic_enum_callback(callback);
  return result(ATOMIC_CALLBACK_SIGNED_VALUE);
}
