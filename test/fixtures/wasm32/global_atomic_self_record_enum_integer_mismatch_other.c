// Paired with global_atomic_self_record_enum_integer_mismatch_main.c.

struct global_atomic_self_record_node {
  int value;
  _Atomic(struct global_atomic_self_record_node *) next;
};

struct global_atomic_self_record_node
    global_atomic_self_record_enum_integer = {42, 0};
