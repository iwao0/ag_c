struct atomic_pointer_record_payload {
  int value;
};

extern _Atomic(struct atomic_pointer_record_payload *)
    global_atomic_pointer_to_record;

int main(void) {
  return global_atomic_pointer_to_record->value;
}
