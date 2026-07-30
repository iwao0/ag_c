enum atomic_packed_layout_signed_enum {
  ATOMIC_PACKED_LAYOUT_NEGATIVE = -1,
  ATOMIC_PACKED_LAYOUT_VALUE = 42
};

#pragma pack(push, 1)
struct atomic_packed_layout_payload {
  char tag;
  enum atomic_packed_layout_signed_enum value;
};
#pragma pack(pop)

int read_atomic_pointer_packed_enum_layout(
    _Atomic(struct atomic_packed_layout_payload *) value);

int main(void) {
  struct atomic_packed_layout_payload value = {
      'p', ATOMIC_PACKED_LAYOUT_VALUE};
  return read_atomic_pointer_packed_enum_layout(&value);
}
