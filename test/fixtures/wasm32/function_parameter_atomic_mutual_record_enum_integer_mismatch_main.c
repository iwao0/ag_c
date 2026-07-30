enum atomic_mutual_record_unsigned_enum {
  ATOMIC_MUTUAL_RECORD_ZERO = 0,
  ATOMIC_MUTUAL_RECORD_VALUE = 42
};

struct atomic_mutual_record_right;

struct atomic_mutual_record_left {
  int value;
  _Atomic(struct atomic_mutual_record_right *) next;
};

struct atomic_mutual_record_right {
  enum atomic_mutual_record_unsigned_enum value;
  _Atomic(struct atomic_mutual_record_left *) next;
};

int read_atomic_mutual_record_enum(
    struct atomic_mutual_record_left *node);

int main(void) {
  struct atomic_mutual_record_right right = {
      ATOMIC_MUTUAL_RECORD_VALUE, 0};
  struct atomic_mutual_record_left left = {0, &right};
  right.next = &left;
  return read_atomic_mutual_record_enum(&left);
}
