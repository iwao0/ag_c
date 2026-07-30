// Paired with atomic_enum_mutual_record_signature_xtu_main.c.

#ifndef AG_C_ATOMIC_ENUM_MUTUAL_RECORD_SIGNATURE_XTU_TYPES
#define AG_C_ATOMIC_ENUM_MUTUAL_RECORD_SIGNATURE_XTU_TYPES
struct atomic_enum_mutual_right;

struct atomic_enum_mutual_left {
  int value;
  _Atomic(struct atomic_enum_mutual_right *) next;
};

struct atomic_enum_mutual_right {
  unsigned int value;
  _Atomic(struct atomic_enum_mutual_left *) next;
};
#endif

static struct atomic_enum_mutual_right partner;
struct atomic_enum_mutual_left atomic_enum_mutual_root = {
    20, &partner};
static struct atomic_enum_mutual_right partner = {
    22U, &atomic_enum_mutual_root};

int sum_atomic_enum_mutual_chain(
    struct atomic_enum_mutual_left *node) {
  struct atomic_enum_mutual_right *next = node->next;
  return node->value + (int)next->value;
}
