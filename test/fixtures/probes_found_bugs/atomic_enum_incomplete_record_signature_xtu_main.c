// An atomic pointer to an incomplete record remains compatible with the
// companion TU's completed record containing an enum member.
// Expected with the companion TU: exit=42.

struct atomic_enum_incomplete_payload;

extern _Atomic(struct atomic_enum_incomplete_payload *)
    shared_atomic_enum_incomplete_payload;

int read_atomic_enum_incomplete_payload(
    _Atomic(struct atomic_enum_incomplete_payload *) value);

int main(void) {
  return read_atomic_enum_incomplete_payload(
      shared_atomic_enum_incomplete_payload);
}
