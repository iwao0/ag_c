// Anonymous unions remain structurally compatible through atomic pointers
// when an enum member corresponds to its compatible integer type.
// Expected with the companion TU: exit=42.

enum atomic_anonymous_union_unsigned_value {
  ATOMIC_ANONYMOUS_UNION_ZERO = 0,
  ATOMIC_ANONYMOUS_UNION_FIRST = 19,
  ATOMIC_ANONYMOUS_UNION_SECOND = 23
};

#ifndef AG_C_ATOMIC_ENUM_ANONYMOUS_UNION_DATA_SIGNATURE_XTU_TYPES
#define AG_C_ATOMIC_ENUM_ANONYMOUS_UNION_DATA_SIGNATURE_XTU_TYPES
typedef union {
  enum atomic_anonymous_union_unsigned_value value;
  unsigned int bits;
} atomic_enum_anonymous_union_t;
#endif

extern _Atomic(atomic_enum_anonymous_union_t *)
    shared_atomic_anonymous_union_pointer;
extern _Atomic(atomic_enum_anonymous_union_t) *
    shared_atomic_anonymous_union_pointee;

int main(void) {
  atomic_enum_anonymous_union_t *plain =
      shared_atomic_anonymous_union_pointer;
  atomic_enum_anonymous_union_t snapshot =
      *shared_atomic_anonymous_union_pointee;
  return plain->value + snapshot.value;
}
