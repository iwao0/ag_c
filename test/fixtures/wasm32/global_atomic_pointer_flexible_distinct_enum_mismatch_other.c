// Paired with global_atomic_pointer_flexible_distinct_enum_mismatch_main.c.

enum global_atomic_flexible_actual_enum {
  GLOBAL_ATOMIC_FLEXIBLE_ACTUAL_ZERO = 0,
  GLOBAL_ATOMIC_FLEXIBLE_ACTUAL_VALUE = 42
};

struct global_atomic_flexible_packet {
  int count;
  _Atomic(enum global_atomic_flexible_actual_enum) values[];
};

_Atomic(struct global_atomic_flexible_packet *)
    global_atomic_pointer_flexible_enum = 0;
