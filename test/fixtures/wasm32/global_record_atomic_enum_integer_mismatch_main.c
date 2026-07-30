enum global_atomic_unsigned_enum {
  GLOBAL_ATOMIC_UNSIGNED_ZERO = 0,
  GLOBAL_ATOMIC_UNSIGNED_VALUE = 42
};

struct global_atomic_enum_record {
  _Atomic(enum global_atomic_unsigned_enum) value;
};

extern struct global_atomic_enum_record
    global_atomic_enum_integer_mismatch;

int main(void) {
  return global_atomic_enum_integer_mismatch.value;
}
