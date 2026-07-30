enum global_atomic_record_expected_enum {
  GLOBAL_ATOMIC_RECORD_EXPECTED_ZERO = 0,
  GLOBAL_ATOMIC_RECORD_EXPECTED_VALUE = 42
};

struct global_atomic_record_enum_payload {
  enum global_atomic_record_expected_enum value;
};

extern _Atomic(struct global_atomic_record_enum_payload *)
    global_atomic_pointer_record_enum_member;

int main(void) {
  return global_atomic_pointer_record_enum_member->value;
}
