// Paired with global_atomic_pointer_anonymous_union_distinct_enum_mismatch_main.c.

enum atomic_anonymous_union_actual_enum {
  ATOMIC_ANONYMOUS_UNION_ACTUAL_ZERO = 0,
  ATOMIC_ANONYMOUS_UNION_ACTUAL_VALUE = 42
};

typedef union {
  int padding;
  enum atomic_anonymous_union_actual_enum value;
} atomic_anonymous_union_actual_t;

static atomic_anonymous_union_actual_t payload = {
    .value = ATOMIC_ANONYMOUS_UNION_ACTUAL_VALUE};

_Atomic(atomic_anonymous_union_actual_t *)
    global_atomic_pointer_anonymous_union_enum = &payload;
