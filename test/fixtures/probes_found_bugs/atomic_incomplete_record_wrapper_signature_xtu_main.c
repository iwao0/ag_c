struct atomic_incomplete_wrapper_payload;

#ifndef AG_C_ATOMIC_INCOMPLETE_RECORD_WRAPPER_SIGNATURE_XTU_TYPES
#define AG_C_ATOMIC_INCOMPLETE_RECORD_WRAPPER_SIGNATURE_XTU_TYPES
struct atomic_incomplete_holder {
  _Atomic(struct atomic_incomplete_wrapper_payload *) member;
};
#endif

extern struct atomic_incomplete_holder
    shared_atomic_incomplete_holder;

int read_atomic_incomplete_holder(
    struct atomic_incomplete_holder value);

int main(void) {
  return read_atomic_incomplete_holder(
      shared_atomic_incomplete_holder);
}
