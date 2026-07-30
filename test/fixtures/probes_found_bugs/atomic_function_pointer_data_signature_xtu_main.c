#ifndef AG_C_ATOMIC_FUNCTION_POINTER_DATA_SIGNATURE_XTU_TYPES
#define AG_C_ATOMIC_FUNCTION_POINTER_DATA_SIGNATURE_XTU_TYPES
typedef int atomic_data_callback_t(int);

struct atomic_callback_holder {
  _Atomic(atomic_data_callback_t *) member;
};
#endif

extern _Atomic(atomic_data_callback_t *) shared_atomic_callback;
extern _Atomic(atomic_data_callback_t *) atomic_callback_slots[2];
extern struct atomic_callback_holder atomic_callback_holder_value;

int main(void) {
  return shared_atomic_callback(10) +
         atomic_callback_slots[1](14) +
         atomic_callback_holder_value.member(18);
}
