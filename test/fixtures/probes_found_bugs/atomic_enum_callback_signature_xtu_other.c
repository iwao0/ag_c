// Paired with atomic_enum_callback_signature_xtu_main.c.

#ifndef AG_C_ATOMIC_ENUM_CALLBACK_SIGNATURE_XTU_TYPES
#define AG_C_ATOMIC_ENUM_CALLBACK_SIGNATURE_XTU_TYPES
typedef int atomic_enum_callback_t(int value);
#endif

_Atomic(atomic_enum_callback_t *)
roundtrip_atomic_enum_callback(
    _Atomic(atomic_enum_callback_t *) callback) {
  return callback;
}
