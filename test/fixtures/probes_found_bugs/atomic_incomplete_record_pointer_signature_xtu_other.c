struct atomic_incomplete_payload {
  int value;
};

static struct atomic_incomplete_payload payload = {42};

_Atomic(struct atomic_incomplete_payload *)
    shared_atomic_incomplete_pointer = &payload;

int read_atomic_incomplete_pointer(
    _Atomic(struct atomic_incomplete_payload *) pointer) {
  return pointer->value;
}
