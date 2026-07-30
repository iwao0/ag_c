#ifndef AG_C_ATOMIC_POINTER_TO_RECORD_SIGNATURE_XTU_TYPES
#define AG_C_ATOMIC_POINTER_TO_RECORD_SIGNATURE_XTU_TYPES
struct atomic_pointer_record_payload {
  int value;
};
#endif

extern _Atomic(struct atomic_pointer_record_payload *)
    shared_atomic_record_pointer;
extern _Atomic(struct atomic_pointer_record_payload) *
    shared_atomic_record_pointee;

int main(void) {
  struct atomic_pointer_record_payload *plain_pointer =
      shared_atomic_record_pointer;
  struct atomic_pointer_record_payload atomic_snapshot =
      *shared_atomic_record_pointee;
  return plain_pointer->value + atomic_snapshot.value;
}
