enum global_atomic_record_pointee_unsigned_enum {
  GLOBAL_ATOMIC_RECORD_POINTEE_ZERO = 0,
  GLOBAL_ATOMIC_RECORD_POINTEE_VALUE = 42
};

struct global_atomic_record_pointee_payload {
  enum global_atomic_record_pointee_unsigned_enum value;
};

extern _Atomic(struct global_atomic_record_pointee_payload) *
    global_atomic_record_pointee_enum_integer;

int main(void) {
  struct global_atomic_record_pointee_payload snapshot =
      *global_atomic_record_pointee_enum_integer;
  return snapshot.value;
}
