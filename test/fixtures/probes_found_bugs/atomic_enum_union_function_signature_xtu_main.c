// Named union members correspond by name regardless of order, and an enum
// member remains compatible with its integer type through atomic pointers.
// Expected with the companion TU: exit=42.

enum atomic_union_signed_value {
  ATOMIC_UNION_SIGNED_NEGATIVE = -1,
  ATOMIC_UNION_SIGNED_FIRST = 20,
  ATOMIC_UNION_SIGNED_SECOND = 22
};

#ifndef AG_C_ATOMIC_ENUM_UNION_FUNCTION_SIGNATURE_XTU_TYPES
#define AG_C_ATOMIC_ENUM_UNION_FUNCTION_SIGNATURE_XTU_TYPES
union atomic_enum_union_function_payload {
  enum atomic_union_signed_value value;
  unsigned int bits;
  unsigned char bytes[4];
};
#endif

_Atomic(union atomic_enum_union_function_payload *)
roundtrip_atomic_enum_union_pointer(
    _Atomic(union atomic_enum_union_function_payload *) value);

int read_atomic_enum_union_pointee(
    _Atomic(union atomic_enum_union_function_payload) *value);

int main(void) {
  union atomic_enum_union_function_payload plain = {
      .value = ATOMIC_UNION_SIGNED_FIRST};
  _Atomic(union atomic_enum_union_function_payload) atomic =
      (union atomic_enum_union_function_payload){
          .value = ATOMIC_UNION_SIGNED_SECOND};
  _Atomic(union atomic_enum_union_function_payload *) pointer =
      &plain;
  _Atomic(union atomic_enum_union_function_payload *) result =
      roundtrip_atomic_enum_union_pointer(pointer);
  return result->value +
         read_atomic_enum_union_pointee(&atomic);
}
