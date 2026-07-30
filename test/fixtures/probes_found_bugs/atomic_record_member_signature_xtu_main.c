#ifndef AG_C_ATOMIC_RECORD_MEMBER_SIGNATURE_XTU_TYPES
#define AG_C_ATOMIC_RECORD_MEMBER_SIGNATURE_XTU_TYPES
struct atomic_record {
  _Atomic(int *) pointer;
  _Atomic(int) *pointee;
};
#endif

extern struct atomic_record shared_atomic_record;

int main(void) {
  return *shared_atomic_record.pointer +
         *shared_atomic_record.pointee;
}
