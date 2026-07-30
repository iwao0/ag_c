// Paired with global_atomic_pointer_packed_record_distinct_enum_mismatch_main.c.

enum global_atomic_packed_actual_enum {
  GLOBAL_ATOMIC_PACKED_ACTUAL_ZERO = 0,
  GLOBAL_ATOMIC_PACKED_ACTUAL_VALUE = 42
};

#pragma pack(push, 1)
struct global_atomic_packed_enum_payload {
  char tag;
  enum global_atomic_packed_actual_enum value;
};
#pragma pack(pop)

static struct global_atomic_packed_enum_payload payload = {
    'p', GLOBAL_ATOMIC_PACKED_ACTUAL_VALUE};

_Atomic(struct global_atomic_packed_enum_payload *)
    global_atomic_pointer_packed_enum = &payload;
