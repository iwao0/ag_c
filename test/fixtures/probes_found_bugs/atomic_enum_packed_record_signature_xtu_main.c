// A packed record reached through an atomic pointer may use an enum member
// where the companion definition uses its compatible integer type.
// Expected with the companion TU: exit=42.

enum atomic_packed_signed_value {
  ATOMIC_PACKED_NEGATIVE = -1,
  ATOMIC_PACKED_VALUE = 42
};

#pragma pack(push, 1)
#ifndef AG_C_ATOMIC_ENUM_PACKED_RECORD_SIGNATURE_XTU_TYPES
#define AG_C_ATOMIC_ENUM_PACKED_RECORD_SIGNATURE_XTU_TYPES
struct atomic_enum_packed_payload {
  char tag;
  enum atomic_packed_signed_value value;
};
#endif
#pragma pack(pop)

_Atomic(struct atomic_enum_packed_payload *)
roundtrip_atomic_enum_packed_record(
    _Atomic(struct atomic_enum_packed_payload *) value);

int main(void) {
  struct atomic_enum_packed_payload value = {
      'p', ATOMIC_PACKED_VALUE};
  _Atomic(struct atomic_enum_packed_payload *) pointer =
      &value;
  _Atomic(struct atomic_enum_packed_payload *) result =
      roundtrip_atomic_enum_packed_record(pointer);
  struct atomic_enum_packed_payload *plain_result = result;
  return plain_result->value;
}
