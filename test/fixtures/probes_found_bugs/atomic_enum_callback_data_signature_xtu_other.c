// Paired with atomic_enum_callback_data_signature_xtu_main.c.

#ifndef AG_C_ATOMIC_ENUM_CALLBACK_DATA_SIGNATURE_XTU_TYPES
#define AG_C_ATOMIC_ENUM_CALLBACK_DATA_SIGNATURE_XTU_TYPES
typedef unsigned int
    atomic_enum_data_callback_t(unsigned int value);

struct atomic_enum_callback_holder {
  _Atomic(atomic_enum_data_callback_t *) member;
};
#endif

static unsigned int identity_atomic_enum_data_callback(
    unsigned int value) {
  return value;
}

_Atomic(atomic_enum_data_callback_t *)
    shared_atomic_enum_callback =
        identity_atomic_enum_data_callback;
_Atomic(atomic_enum_data_callback_t *)
    shared_atomic_enum_callback_slots[2] = {
        identity_atomic_enum_data_callback,
        identity_atomic_enum_data_callback};
struct atomic_enum_callback_holder
    shared_atomic_enum_callback_holder = {
        identity_atomic_enum_data_callback};
