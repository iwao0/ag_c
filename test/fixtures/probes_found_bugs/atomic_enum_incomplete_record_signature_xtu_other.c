// Paired with atomic_enum_incomplete_record_signature_xtu_main.c.

enum atomic_incomplete_signed_value {
  ATOMIC_INCOMPLETE_NEGATIVE = -1,
  ATOMIC_INCOMPLETE_VALUE = 42
};

struct atomic_enum_incomplete_payload {
  enum atomic_incomplete_signed_value value;
};

static struct atomic_enum_incomplete_payload payload = {
    ATOMIC_INCOMPLETE_VALUE};

_Atomic(struct atomic_enum_incomplete_payload *)
    shared_atomic_enum_incomplete_payload = &payload;

int read_atomic_enum_incomplete_payload(
    _Atomic(struct atomic_enum_incomplete_payload *) value) {
  return value->value;
}
