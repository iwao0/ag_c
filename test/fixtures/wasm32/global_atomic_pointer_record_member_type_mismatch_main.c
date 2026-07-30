struct global_atomic_member_payload {
  int value;
};

extern _Atomic(struct global_atomic_member_payload *)
    global_atomic_pointer_record_member;

int main(void) {
  return global_atomic_pointer_record_member->value;
}
