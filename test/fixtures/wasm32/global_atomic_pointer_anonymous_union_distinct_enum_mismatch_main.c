enum atomic_anonymous_union_expected_enum {
  ATOMIC_ANONYMOUS_UNION_EXPECTED_ZERO = 0,
  ATOMIC_ANONYMOUS_UNION_EXPECTED_VALUE = 42
};

typedef union {
  enum atomic_anonymous_union_expected_enum value;
  int padding;
} atomic_anonymous_union_expected_t;

extern _Atomic(atomic_anonymous_union_expected_t *)
    global_atomic_pointer_anonymous_union_enum;

int main(void) {
  return global_atomic_pointer_anonymous_union_enum->value;
}
