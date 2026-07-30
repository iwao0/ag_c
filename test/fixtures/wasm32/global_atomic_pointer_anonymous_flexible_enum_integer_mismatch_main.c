enum atomic_anonymous_flexible_unsigned_enum {
  ATOMIC_ANONYMOUS_FLEXIBLE_ZERO = 0,
  ATOMIC_ANONYMOUS_FLEXIBLE_VALUE = 42
};

typedef struct {
  int count;
  _Atomic(enum atomic_anonymous_flexible_unsigned_enum) values[];
} atomic_anonymous_flexible_expected_t;

extern _Atomic(atomic_anonymous_flexible_expected_t *)
    global_atomic_pointer_anonymous_flexible_enum;

int main(void) {
  return global_atomic_pointer_anonymous_flexible_enum == 0
             ? 42
             : 0;
}
