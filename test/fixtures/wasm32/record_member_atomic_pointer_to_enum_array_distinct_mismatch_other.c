// Paired with record_member_atomic_pointer_to_enum_array_distinct_mismatch_main.c.

enum record_atomic_enum_array_actual {
  RECORD_ATOMIC_ENUM_ARRAY_ACTUAL_ZERO = 0,
  RECORD_ATOMIC_ENUM_ARRAY_ACTUAL_VALUE = 42
};

struct record_atomic_enum_array_holder {
  _Atomic(enum record_atomic_enum_array_actual (*)[2]) member;
};

static enum record_atomic_enum_array_actual
    record_atomic_enum_array_payload[2] = {
        RECORD_ATOMIC_ENUM_ARRAY_ACTUAL_ZERO,
        RECORD_ATOMIC_ENUM_ARRAY_ACTUAL_VALUE};

struct record_atomic_enum_array_holder
    record_atomic_enum_array_value = {
        &record_atomic_enum_array_payload};
