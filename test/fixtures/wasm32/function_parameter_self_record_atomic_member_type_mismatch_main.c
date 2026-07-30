struct self_atomic_member_node {
  int value;
  _Atomic(struct self_atomic_member_node *) next;
};

int function_parameter_self_record_atomic_member(
    struct self_atomic_member_node *node);

int main(void) {
  struct self_atomic_member_node node = {42, 0};
  return function_parameter_self_record_atomic_member(&node);
}
