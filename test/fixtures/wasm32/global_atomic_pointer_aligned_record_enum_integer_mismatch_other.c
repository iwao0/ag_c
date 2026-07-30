// Paired with global_atomic_pointer_aligned_record_enum_integer_mismatch_main.c.

struct global_atomic_aligned_enum_payload {
  char tag;
  _Alignas(8) int value;
};

static struct global_atomic_aligned_enum_payload payload = {
    'a', 42};

_Atomic(struct global_atomic_aligned_enum_payload *)
    global_atomic_pointer_aligned_enum = &payload;
