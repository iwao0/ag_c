// Paired with global_atomic_pointer_anonymous_flexible_enum_integer_mismatch_main.c.

typedef struct {
  int count;
  _Atomic(int) values[];
} atomic_anonymous_flexible_actual_t;

_Atomic(atomic_anonymous_flexible_actual_t *)
    global_atomic_pointer_anonymous_flexible_enum = 0;
