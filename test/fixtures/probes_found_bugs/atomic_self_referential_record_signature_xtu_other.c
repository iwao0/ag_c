#ifndef AG_C_ATOMIC_SELF_REFERENTIAL_RECORD_SIGNATURE_XTU_TYPES
#define AG_C_ATOMIC_SELF_REFERENTIAL_RECORD_SIGNATURE_XTU_TYPES
struct atomic_self_node {
  int value;
  _Atomic(struct atomic_self_node *) next;
};
#endif

static struct atomic_self_node atomic_self_tail = {22, 0};
struct atomic_self_node atomic_self_head = {
    20, &atomic_self_tail};

int sum_atomic_self_chain(struct atomic_self_node *node) {
  struct atomic_self_node *next = node->next;
  return node->value + next->value;
}
