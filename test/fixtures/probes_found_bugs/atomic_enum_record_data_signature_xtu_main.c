// Canonical data signatures compare enum-compatible record members through
// both atomic pointer and pointer-to-atomic-record positions.
// Expected with the companion TU: exit=42.

enum atomic_record_unsigned_value {
  ATOMIC_RECORD_UNSIGNED_ZERO = 0,
  ATOMIC_RECORD_UNSIGNED_FIRST = 19,
  ATOMIC_RECORD_UNSIGNED_SECOND = 23
};

#ifndef AG_C_ATOMIC_ENUM_RECORD_DATA_SIGNATURE_XTU_TYPES
#define AG_C_ATOMIC_ENUM_RECORD_DATA_SIGNATURE_XTU_TYPES
struct atomic_enum_record_data_payload {
  enum atomic_record_unsigned_value value;
};
#endif

extern _Atomic(struct atomic_enum_record_data_payload *)
    shared_atomic_enum_record_pointer;
extern _Atomic(struct atomic_enum_record_data_payload) *
    shared_atomic_enum_record_pointee;

int main(void) {
  struct atomic_enum_record_data_payload *plain =
      shared_atomic_enum_record_pointer;
  struct atomic_enum_record_data_payload snapshot =
      *shared_atomic_enum_record_pointee;
  return plain->value + snapshot.value;
}
