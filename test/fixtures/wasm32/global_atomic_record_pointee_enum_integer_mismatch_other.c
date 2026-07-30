// Paired with global_atomic_record_pointee_enum_integer_mismatch_main.c.

struct global_atomic_record_pointee_payload {
  int value;
};

static _Atomic(struct global_atomic_record_pointee_payload)
    payload =
        (struct global_atomic_record_pointee_payload){42};

_Atomic(struct global_atomic_record_pointee_payload) *
    global_atomic_record_pointee_enum_integer = &payload;
