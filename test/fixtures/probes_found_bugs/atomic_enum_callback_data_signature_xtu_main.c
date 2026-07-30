// Atomic callback pointers in globals, arrays, and record members retain enum
// compatibility at the callback parameter and result positions.
// Expected with the companion TU: exit=42.

enum atomic_callback_unsigned_value {
  ATOMIC_CALLBACK_UNSIGNED_ZERO = 0,
  ATOMIC_CALLBACK_UNSIGNED_VALUE = 42
};

#ifndef AG_C_ATOMIC_ENUM_CALLBACK_DATA_SIGNATURE_XTU_TYPES
#define AG_C_ATOMIC_ENUM_CALLBACK_DATA_SIGNATURE_XTU_TYPES
typedef enum atomic_callback_unsigned_value
    atomic_enum_data_callback_t(
        enum atomic_callback_unsigned_value value);

struct atomic_enum_callback_holder {
  _Atomic(atomic_enum_data_callback_t *) member;
};
#endif

extern _Atomic(atomic_enum_data_callback_t *)
    shared_atomic_enum_callback;
extern _Atomic(atomic_enum_data_callback_t *)
    shared_atomic_enum_callback_slots[2];
extern struct atomic_enum_callback_holder
    shared_atomic_enum_callback_holder;

int main(void) {
  return shared_atomic_enum_callback(10) +
         shared_atomic_enum_callback_slots[1](14) +
         shared_atomic_enum_callback_holder.member(18);
}
