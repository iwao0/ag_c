struct atomic_incomplete_payload;

extern _Atomic(struct atomic_incomplete_payload *)
    shared_atomic_incomplete_pointer;

int read_atomic_incomplete_pointer(
    _Atomic(struct atomic_incomplete_payload *) pointer);

int main(void) {
  return read_atomic_incomplete_pointer(
      shared_atomic_incomplete_pointer);
}
