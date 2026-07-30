#ifndef AG_C_ATOMIC_MUTUAL_RECORD_SIGNATURE_XTU_TYPES
#define AG_C_ATOMIC_MUTUAL_RECORD_SIGNATURE_XTU_TYPES
struct atomic_mutual_right;

struct atomic_mutual_left {
  int value;
  _Atomic(struct atomic_mutual_right *) next;
};

struct atomic_mutual_right {
  int value;
  _Atomic(struct atomic_mutual_left *) next;
};
#endif

static struct atomic_mutual_right atomic_mutual_partner;
struct atomic_mutual_left atomic_mutual_root = {
    20, &atomic_mutual_partner};
static struct atomic_mutual_right atomic_mutual_partner = {
    22, &atomic_mutual_root};

int sum_atomic_mutual_chain(struct atomic_mutual_left *node) {
  struct atomic_mutual_right *next = node->next;
  return node->value + next->value;
}
