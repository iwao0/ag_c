// Paired with atomic_enum_packed_record_signature_xtu_main.c.

#pragma pack(push, 1)
#ifndef AG_C_ATOMIC_ENUM_PACKED_RECORD_SIGNATURE_XTU_TYPES
#define AG_C_ATOMIC_ENUM_PACKED_RECORD_SIGNATURE_XTU_TYPES
struct atomic_enum_packed_payload {
  char tag;
  int value;
};
#endif
#pragma pack(pop)

_Atomic(struct atomic_enum_packed_payload *)
roundtrip_atomic_enum_packed_record(
    _Atomic(struct atomic_enum_packed_payload *) value) {
  return value;
}
