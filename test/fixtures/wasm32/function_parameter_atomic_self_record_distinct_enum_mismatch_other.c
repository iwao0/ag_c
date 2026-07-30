// Paired with function_parameter_atomic_self_record_distinct_enum_mismatch_main.c.

enum atomic_self_record_actual_enum {
  ATOMIC_SELF_RECORD_ACTUAL_ZERO = 0,
  ATOMIC_SELF_RECORD_ACTUAL_VALUE = 42
};

struct atomic_self_record_enum_node {
  enum atomic_self_record_actual_enum value;
  _Atomic(struct atomic_self_record_enum_node *) next;
};

int read_atomic_self_record_enum(
    struct atomic_self_record_enum_node *node) {
  return node->value;
}
