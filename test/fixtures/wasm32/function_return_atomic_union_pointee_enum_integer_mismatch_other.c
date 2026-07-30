// Paired with function_return_atomic_union_pointee_enum_integer_mismatch_main.c.

union atomic_union_return_payload {
  unsigned int bits;
  int value;
};

static _Atomic(union atomic_union_return_payload) payload =
    (union atomic_union_return_payload){.value = 42};

_Atomic(union atomic_union_return_payload) *
get_atomic_union_pointee_enum_integer(void) {
  return &payload;
}
