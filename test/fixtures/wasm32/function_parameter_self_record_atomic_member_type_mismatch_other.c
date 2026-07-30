struct self_atomic_member_node {
  unsigned int value;
  _Atomic(struct self_atomic_member_node *) next;
};

int function_parameter_self_record_atomic_member(
    struct self_atomic_member_node *node) {
  return (int)node->value;
}
