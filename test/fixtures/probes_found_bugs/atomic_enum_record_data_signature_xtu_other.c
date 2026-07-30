// Paired with atomic_enum_record_data_signature_xtu_main.c.

#ifndef AG_C_ATOMIC_ENUM_RECORD_DATA_SIGNATURE_XTU_TYPES
#define AG_C_ATOMIC_ENUM_RECORD_DATA_SIGNATURE_XTU_TYPES
struct atomic_enum_record_data_payload {
  unsigned int value;
};
#endif

static struct atomic_enum_record_data_payload plain_payload = {
    19U};
static _Atomic(struct atomic_enum_record_data_payload)
    atomic_payload =
        (struct atomic_enum_record_data_payload){23U};

_Atomic(struct atomic_enum_record_data_payload *)
    shared_atomic_enum_record_pointer = &plain_payload;
_Atomic(struct atomic_enum_record_data_payload) *
    shared_atomic_enum_record_pointee = &atomic_payload;
