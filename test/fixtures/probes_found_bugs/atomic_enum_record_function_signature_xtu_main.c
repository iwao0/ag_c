// Enum compatibility is preserved in a record body reached through an atomic
// pointer and through a pointer to an atomic record.
// Expected with the companion TU: exit=42.

enum atomic_record_signed_value {
  ATOMIC_RECORD_SIGNED_NEGATIVE = -1,
  ATOMIC_RECORD_SIGNED_FIRST = 20,
  ATOMIC_RECORD_SIGNED_SECOND = 22
};

#ifndef AG_C_ATOMIC_ENUM_RECORD_FUNCTION_SIGNATURE_XTU_TYPES
#define AG_C_ATOMIC_ENUM_RECORD_FUNCTION_SIGNATURE_XTU_TYPES
struct atomic_enum_record_function_payload {
  enum atomic_record_signed_value value;
};
#endif

_Atomic(struct atomic_enum_record_function_payload *)
roundtrip_atomic_enum_record_pointer(
    _Atomic(struct atomic_enum_record_function_payload *) value);

int read_atomic_enum_record_pointee(
    _Atomic(struct atomic_enum_record_function_payload) *value);

int main(void) {
  struct atomic_enum_record_function_payload plain = {
      ATOMIC_RECORD_SIGNED_FIRST};
  _Atomic(struct atomic_enum_record_function_payload) atomic =
      (struct atomic_enum_record_function_payload){
          ATOMIC_RECORD_SIGNED_SECOND};
  _Atomic(struct atomic_enum_record_function_payload *) pointer =
      &plain;
  _Atomic(struct atomic_enum_record_function_payload *) result =
      roundtrip_atomic_enum_record_pointer(pointer);
  return result->value +
         read_atomic_enum_record_pointee(&atomic);
}
