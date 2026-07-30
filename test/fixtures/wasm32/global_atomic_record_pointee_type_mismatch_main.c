struct atomic_record_pointee_payload {
  int value;
};

extern _Atomic(struct atomic_record_pointee_payload) *
    global_atomic_record_pointee;

int main(void) {
  struct atomic_record_pointee_payload snapshot =
      *global_atomic_record_pointee;
  return snapshot.value;
}
