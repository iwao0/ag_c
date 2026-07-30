// Mutually recursive records connected by atomic pointers preserve compatible
// enum/integer members on both sides of the cycle.
// Expected with the companion TU: exit=42.

enum atomic_mutual_signed_value {
  ATOMIC_MUTUAL_SIGNED_NEGATIVE = -1,
  ATOMIC_MUTUAL_SIGNED_VALUE = 20
};

enum atomic_mutual_unsigned_value {
  ATOMIC_MUTUAL_UNSIGNED_ZERO = 0,
  ATOMIC_MUTUAL_UNSIGNED_VALUE = 22
};

#ifndef AG_C_ATOMIC_ENUM_MUTUAL_RECORD_SIGNATURE_XTU_TYPES
#define AG_C_ATOMIC_ENUM_MUTUAL_RECORD_SIGNATURE_XTU_TYPES
struct atomic_enum_mutual_right;

struct atomic_enum_mutual_left {
  enum atomic_mutual_signed_value value;
  _Atomic(struct atomic_enum_mutual_right *) next;
};

struct atomic_enum_mutual_right {
  enum atomic_mutual_unsigned_value value;
  _Atomic(struct atomic_enum_mutual_left *) next;
};
#endif

extern struct atomic_enum_mutual_left
    atomic_enum_mutual_root;

int sum_atomic_enum_mutual_chain(
    struct atomic_enum_mutual_left *node);

int main(void) {
  return sum_atomic_enum_mutual_chain(
      &atomic_enum_mutual_root);
}
