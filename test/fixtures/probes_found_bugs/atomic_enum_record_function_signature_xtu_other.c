// Paired with atomic_enum_record_function_signature_xtu_main.c.

#ifndef AG_C_ATOMIC_ENUM_RECORD_FUNCTION_SIGNATURE_XTU_TYPES
#define AG_C_ATOMIC_ENUM_RECORD_FUNCTION_SIGNATURE_XTU_TYPES
struct atomic_enum_record_function_payload {
  int value;
};
#endif

_Atomic(struct atomic_enum_record_function_payload *)
roundtrip_atomic_enum_record_pointer(
    _Atomic(struct atomic_enum_record_function_payload *) value) {
  return value;
}

int read_atomic_enum_record_pointee(
    _Atomic(struct atomic_enum_record_function_payload) *value) {
  struct atomic_enum_record_function_payload snapshot = *value;
  return snapshot.value;
}
