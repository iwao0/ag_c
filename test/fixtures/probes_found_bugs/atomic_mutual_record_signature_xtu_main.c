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

extern struct atomic_mutual_left atomic_mutual_root;

int sum_atomic_mutual_chain(struct atomic_mutual_left *node);

int main(void) {
  return sum_atomic_mutual_chain(&atomic_mutual_root);
}
