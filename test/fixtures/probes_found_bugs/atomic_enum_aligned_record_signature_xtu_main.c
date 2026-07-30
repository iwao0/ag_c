// Explicit member alignment is retained while an enum member is matched to
// its compatible integer type through an atomic pointer data signature.
// Expected with the companion TU: exit=42.

enum atomic_aligned_unsigned_value {
  ATOMIC_ALIGNED_ZERO = 0,
  ATOMIC_ALIGNED_VALUE = 42
};

#ifndef AG_C_ATOMIC_ENUM_ALIGNED_RECORD_SIGNATURE_XTU_TYPES
#define AG_C_ATOMIC_ENUM_ALIGNED_RECORD_SIGNATURE_XTU_TYPES
struct atomic_enum_aligned_payload {
  char tag;
  _Alignas(8) enum atomic_aligned_unsigned_value value;
};
#endif

extern _Atomic(struct atomic_enum_aligned_payload *)
    shared_atomic_enum_aligned_record;

int main(void) {
  return shared_atomic_enum_aligned_record->value;
}
