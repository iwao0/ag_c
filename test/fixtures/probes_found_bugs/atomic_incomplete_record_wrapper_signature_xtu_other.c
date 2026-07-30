struct atomic_incomplete_wrapper_payload {
  int value;
};

#ifndef AG_C_ATOMIC_INCOMPLETE_RECORD_WRAPPER_SIGNATURE_XTU_TYPES
#define AG_C_ATOMIC_INCOMPLETE_RECORD_WRAPPER_SIGNATURE_XTU_TYPES
struct atomic_incomplete_holder {
  _Atomic(struct atomic_incomplete_wrapper_payload *) member;
};
#endif

static struct atomic_incomplete_wrapper_payload payload = {42};

struct atomic_incomplete_holder
    shared_atomic_incomplete_holder = {&payload};

int read_atomic_incomplete_holder(
    struct atomic_incomplete_holder value) {
  return value.member->value;
}
