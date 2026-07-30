// Paired with atomic_enum_union_function_signature_xtu_main.c.

#ifndef AG_C_ATOMIC_ENUM_UNION_FUNCTION_SIGNATURE_XTU_TYPES
#define AG_C_ATOMIC_ENUM_UNION_FUNCTION_SIGNATURE_XTU_TYPES
union atomic_enum_union_function_payload {
  unsigned char bytes[4];
  unsigned int bits;
  int value;
};
#endif

_Atomic(union atomic_enum_union_function_payload *)
roundtrip_atomic_enum_union_pointer(
    _Atomic(union atomic_enum_union_function_payload *) value) {
  return value;
}

int read_atomic_enum_union_pointee(
    _Atomic(union atomic_enum_union_function_payload) *value) {
  union atomic_enum_union_function_payload snapshot = *value;
  return snapshot.value;
}
