struct atomic_pointer_record {
  _Atomic(int *) pointer;
};

extern struct atomic_pointer_record
    record_member_atomic_pointer_value;

int main(void) {
  return record_member_atomic_pointer_value.pointer != 0;
}
