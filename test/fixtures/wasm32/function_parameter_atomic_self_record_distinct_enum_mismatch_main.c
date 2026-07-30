enum atomic_self_record_expected_enum {
  ATOMIC_SELF_RECORD_EXPECTED_ZERO = 0,
  ATOMIC_SELF_RECORD_EXPECTED_VALUE = 42
};

struct atomic_self_record_enum_node {
  enum atomic_self_record_expected_enum value;
  _Atomic(struct atomic_self_record_enum_node *) next;
};

int read_atomic_self_record_enum(
    struct atomic_self_record_enum_node *node);

int main(void) {
  struct atomic_self_record_enum_node node = {
      ATOMIC_SELF_RECORD_EXPECTED_VALUE, 0};
  return read_atomic_self_record_enum(&node);
}
