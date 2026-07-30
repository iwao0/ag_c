struct self_atomic_qualifier_node {
  int value;
  struct self_atomic_qualifier_node *next;
};

int function_parameter_self_record_atomic_pointer_qualifier(
    struct self_atomic_qualifier_node *node) {
  return node->value;
}
