#ifndef AG_C_ATOMIC_SELF_REFERENTIAL_RECORD_SIGNATURE_XTU_TYPES
#define AG_C_ATOMIC_SELF_REFERENTIAL_RECORD_SIGNATURE_XTU_TYPES
struct atomic_self_node {
  int value;
  _Atomic(struct atomic_self_node *) next;
};
#endif

extern struct atomic_self_node atomic_self_head;

int sum_atomic_self_chain(struct atomic_self_node *node);

int main(void) {
  return sum_atomic_self_chain(&atomic_self_head);
}
