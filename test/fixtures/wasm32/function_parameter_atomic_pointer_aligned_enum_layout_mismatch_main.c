enum atomic_aligned_layout_unsigned_enum {
  ATOMIC_ALIGNED_LAYOUT_ZERO = 0,
  ATOMIC_ALIGNED_LAYOUT_VALUE = 42
};

struct atomic_aligned_layout_payload {
  char tag;
  _Alignas(8) enum atomic_aligned_layout_unsigned_enum value;
};

int read_atomic_pointer_aligned_enum_layout(
    _Atomic(struct atomic_aligned_layout_payload *) value);

int main(void) {
  struct atomic_aligned_layout_payload value = {
      'a', ATOMIC_ALIGNED_LAYOUT_VALUE};
  return read_atomic_pointer_aligned_enum_layout(&value);
}
