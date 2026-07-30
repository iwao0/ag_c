enum global_atomic_packed_expected_enum {
  GLOBAL_ATOMIC_PACKED_EXPECTED_ZERO = 0,
  GLOBAL_ATOMIC_PACKED_EXPECTED_VALUE = 42
};

#pragma pack(push, 1)
struct global_atomic_packed_enum_payload {
  char tag;
  enum global_atomic_packed_expected_enum value;
};
#pragma pack(pop)

extern _Atomic(struct global_atomic_packed_enum_payload *)
    global_atomic_pointer_packed_enum;

int main(void) {
  return global_atomic_pointer_packed_enum->value;
}
