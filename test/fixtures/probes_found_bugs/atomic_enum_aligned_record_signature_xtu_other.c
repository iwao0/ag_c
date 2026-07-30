// Paired with atomic_enum_aligned_record_signature_xtu_main.c.

#ifndef AG_C_ATOMIC_ENUM_ALIGNED_RECORD_SIGNATURE_XTU_TYPES
#define AG_C_ATOMIC_ENUM_ALIGNED_RECORD_SIGNATURE_XTU_TYPES
struct atomic_enum_aligned_payload {
  char tag;
  _Alignas(8) unsigned int value;
};
#endif

static struct atomic_enum_aligned_payload payload = {
    'a', 42U};

_Atomic(struct atomic_enum_aligned_payload *)
    shared_atomic_enum_aligned_record = &payload;
