// Paired with global_atomic_pointer_record_distinct_enum_member_mismatch_main.c.

enum global_atomic_record_actual_enum {
  GLOBAL_ATOMIC_RECORD_ACTUAL_ZERO = 0,
  GLOBAL_ATOMIC_RECORD_ACTUAL_VALUE = 42
};

struct global_atomic_record_enum_payload {
  enum global_atomic_record_actual_enum value;
};

static struct global_atomic_record_enum_payload payload = {
    GLOBAL_ATOMIC_RECORD_ACTUAL_VALUE};

_Atomic(struct global_atomic_record_enum_payload *)
    global_atomic_pointer_record_enum_member = &payload;
