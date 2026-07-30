enum global_atomic_aligned_unsigned_enum {
  GLOBAL_ATOMIC_ALIGNED_ZERO = 0,
  GLOBAL_ATOMIC_ALIGNED_VALUE = 42
};

struct global_atomic_aligned_enum_payload {
  char tag;
  _Alignas(8) enum global_atomic_aligned_unsigned_enum value;
};

extern _Atomic(struct global_atomic_aligned_enum_payload *)
    global_atomic_pointer_aligned_enum;

int main(void) {
  return global_atomic_pointer_aligned_enum->value;
}
