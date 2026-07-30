#ifndef AG_C_ATOMIC_FUNCTION_POINTER_SIGNATURE_XTU_TYPES
#define AG_C_ATOMIC_FUNCTION_POINTER_SIGNATURE_XTU_TYPES
typedef int atomic_callback_t(int);
#endif

_Atomic(atomic_callback_t *) roundtrip_atomic_callback(
    _Atomic(atomic_callback_t *) callback) {
  return callback;
}
