enum atomic_anonymous_union_pointee_unsigned_enum {
  ATOMIC_ANONYMOUS_UNION_POINTEE_ZERO = 0,
  ATOMIC_ANONYMOUS_UNION_POINTEE_VALUE = 42
};

typedef union {
  enum atomic_anonymous_union_pointee_unsigned_enum value;
  unsigned int bits;
} atomic_anonymous_union_pointee_expected_t;

extern _Atomic(atomic_anonymous_union_pointee_expected_t) *
    global_atomic_anonymous_union_pointee_enum;

int main(void) {
  atomic_anonymous_union_pointee_expected_t snapshot =
      *global_atomic_anonymous_union_pointee_enum;
  return snapshot.value;
}
