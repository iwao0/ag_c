#ifndef AG_C_ATOMIC_FUNCTION_POINTER_SIGNATURE_XTU_TYPES
#define AG_C_ATOMIC_FUNCTION_POINTER_SIGNATURE_XTU_TYPES
typedef int atomic_callback_t(int);
#endif

_Atomic(atomic_callback_t *) roundtrip_atomic_callback(
    const volatile _Atomic(atomic_callback_t *) callback);

static int add_one(int value) {
  return value + 1;
}

int main(void) {
  _Atomic(atomic_callback_t *) callback = add_one;
  _Atomic(atomic_callback_t *) result =
      roundtrip_atomic_callback(callback);
  return result(41);
}
