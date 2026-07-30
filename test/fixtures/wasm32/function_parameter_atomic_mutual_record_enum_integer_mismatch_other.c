// Paired with function_parameter_atomic_mutual_record_enum_integer_mismatch_main.c.

struct atomic_mutual_record_right;

struct atomic_mutual_record_left {
  int value;
  _Atomic(struct atomic_mutual_record_right *) next;
};

struct atomic_mutual_record_right {
  int value;
  _Atomic(struct atomic_mutual_record_left *) next;
};

int read_atomic_mutual_record_enum(
    struct atomic_mutual_record_left *node) {
  struct atomic_mutual_record_right *next = node->next;
  return next->value;
}
