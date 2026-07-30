// Paired with global_atomic_anonymous_union_pointee_enum_integer_mismatch_main.c.

typedef union {
  unsigned int bits;
  int value;
} atomic_anonymous_union_pointee_actual_t;

static _Atomic(atomic_anonymous_union_pointee_actual_t) payload =
    (atomic_anonymous_union_pointee_actual_t){.value = 42};

_Atomic(atomic_anonymous_union_pointee_actual_t) *
    global_atomic_anonymous_union_pointee_enum = &payload;
