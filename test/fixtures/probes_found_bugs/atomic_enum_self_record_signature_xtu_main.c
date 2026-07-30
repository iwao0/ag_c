// A self-referential record reached through an atomic pointer may use an enum
// member where the companion definition uses its compatible integer type.
// Expected with the companion TU: exit=42.

enum atomic_self_enum_value {
  ATOMIC_SELF_ENUM_NEGATIVE = -1,
  ATOMIC_SELF_ENUM_FIRST = 20,
  ATOMIC_SELF_ENUM_SECOND = 22
};

#ifndef AG_C_ATOMIC_ENUM_SELF_RECORD_SIGNATURE_XTU_TYPES
#define AG_C_ATOMIC_ENUM_SELF_RECORD_SIGNATURE_XTU_TYPES
struct atomic_enum_self_node {
  enum atomic_self_enum_value value;
  _Atomic(struct atomic_enum_self_node *) next;
};
#endif

extern struct atomic_enum_self_node atomic_enum_self_head;

int sum_atomic_enum_self_chain(
    struct atomic_enum_self_node *node);

int main(void) {
  return sum_atomic_enum_self_chain(
      &atomic_enum_self_head);
}
