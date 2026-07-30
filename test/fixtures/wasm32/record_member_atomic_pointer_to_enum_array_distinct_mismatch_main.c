enum record_atomic_enum_array_expected {
  RECORD_ATOMIC_ENUM_ARRAY_EXPECTED_ZERO = 0,
  RECORD_ATOMIC_ENUM_ARRAY_EXPECTED_VALUE = 42
};

struct record_atomic_enum_array_holder {
  _Atomic(enum record_atomic_enum_array_expected (*)[2]) member;
};

extern struct record_atomic_enum_array_holder
    record_atomic_enum_array_value;

int main(void) {
  return (*record_atomic_enum_array_value.member)[1];
}
