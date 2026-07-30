struct atomic_pointee_record {
  _Atomic(int) *pointee;
};

extern struct atomic_pointee_record
    record_member_atomic_pointee_value;

int main(void) {
  return *record_member_atomic_pointee_value.pointee;
}
