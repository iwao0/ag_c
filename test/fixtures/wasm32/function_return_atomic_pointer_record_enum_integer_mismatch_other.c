// Paired with function_return_atomic_pointer_record_enum_integer_mismatch_main.c.

struct atomic_record_return_payload {
  int value;
};

static struct atomic_record_return_payload payload = {42};

_Atomic(struct atomic_record_return_payload *)
get_atomic_pointer_record_enum_integer(void) {
  return &payload;
}
