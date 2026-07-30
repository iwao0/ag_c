#ifndef AG_C_ATOMIC_FUNCTION_POINTER_DATA_SIGNATURE_XTU_TYPES
#define AG_C_ATOMIC_FUNCTION_POINTER_DATA_SIGNATURE_XTU_TYPES
typedef int atomic_data_callback_t(int);

struct atomic_callback_holder {
  _Atomic(atomic_data_callback_t *) member;
};
#endif

static int identity(int value) {
  return value;
}

_Atomic(atomic_data_callback_t *)
    shared_atomic_callback = identity;
_Atomic(atomic_data_callback_t *)
    atomic_callback_slots[2] = {identity, identity};
struct atomic_callback_holder atomic_callback_holder_value = {
    identity};
