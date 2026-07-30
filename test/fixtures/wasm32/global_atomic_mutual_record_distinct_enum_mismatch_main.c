enum global_atomic_mutual_expected_enum {
  GLOBAL_ATOMIC_MUTUAL_EXPECTED_ZERO = 0,
  GLOBAL_ATOMIC_MUTUAL_EXPECTED_VALUE = 42
};

struct global_atomic_mutual_right;

struct global_atomic_mutual_left {
  int value;
  _Atomic(struct global_atomic_mutual_right *) next;
};

struct global_atomic_mutual_right {
  enum global_atomic_mutual_expected_enum value;
  _Atomic(struct global_atomic_mutual_left *) next;
};

extern struct global_atomic_mutual_left
    global_atomic_mutual_record_enum;

int main(void) {
  return global_atomic_mutual_record_enum.next->value;
}
