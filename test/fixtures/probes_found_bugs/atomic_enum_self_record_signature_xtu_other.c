// Paired with atomic_enum_self_record_signature_xtu_main.c.

#ifndef AG_C_ATOMIC_ENUM_SELF_RECORD_SIGNATURE_XTU_TYPES
#define AG_C_ATOMIC_ENUM_SELF_RECORD_SIGNATURE_XTU_TYPES
struct atomic_enum_self_node {
  int value;
  _Atomic(struct atomic_enum_self_node *) next;
};
#endif

static struct atomic_enum_self_node tail = {22, 0};
struct atomic_enum_self_node atomic_enum_self_head = {
    20, &tail};

int sum_atomic_enum_self_chain(
    struct atomic_enum_self_node *node) {
  struct atomic_enum_self_node *next = node->next;
  return node->value + next->value;
}
