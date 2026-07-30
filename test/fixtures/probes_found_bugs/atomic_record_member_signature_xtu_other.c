#ifndef AG_C_ATOMIC_RECORD_MEMBER_SIGNATURE_XTU_TYPES
#define AG_C_ATOMIC_RECORD_MEMBER_SIGNATURE_XTU_TYPES
struct atomic_record {
  _Atomic(int *) pointer;
  _Atomic(int) *pointee;
};
#endif

static int plain_value = 20;
static _Atomic(int) atomic_value = 22;

struct atomic_record shared_atomic_record = {
    &plain_value, &atomic_value};
