enum global_atomic_flexible_expected_enum {
  GLOBAL_ATOMIC_FLEXIBLE_EXPECTED_ZERO = 0,
  GLOBAL_ATOMIC_FLEXIBLE_EXPECTED_VALUE = 42
};

struct global_atomic_flexible_packet {
  int count;
  _Atomic(enum global_atomic_flexible_expected_enum) values[];
};

extern _Atomic(struct global_atomic_flexible_packet *)
    global_atomic_pointer_flexible_enum;

int main(void) {
  return global_atomic_pointer_flexible_enum == 0 ? 42 : 0;
}
