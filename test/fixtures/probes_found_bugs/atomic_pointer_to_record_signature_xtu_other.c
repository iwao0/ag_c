#ifndef AG_C_ATOMIC_POINTER_TO_RECORD_SIGNATURE_XTU_TYPES
#define AG_C_ATOMIC_POINTER_TO_RECORD_SIGNATURE_XTU_TYPES
struct atomic_pointer_record_payload {
  int value;
};
#endif

static struct atomic_pointer_record_payload plain_payload = {20};
static _Atomic(struct atomic_pointer_record_payload)
    atomic_payload =
        (struct atomic_pointer_record_payload){22};

_Atomic(struct atomic_pointer_record_payload *)
    shared_atomic_record_pointer = &plain_payload;
_Atomic(struct atomic_pointer_record_payload) *
    shared_atomic_record_pointee = &atomic_payload;
