enum atomic_pointee_expected_enum {
  ATOMIC_POINTEE_EXPECTED_ZERO = 0,
  ATOMIC_POINTEE_EXPECTED_VALUE = 42
};

int read_atomic_pointee_distinct_enum(
    _Atomic(enum atomic_pointee_expected_enum) *value);

int main(void) {
  _Atomic(enum atomic_pointee_expected_enum) value =
      ATOMIC_POINTEE_EXPECTED_VALUE;
  return read_atomic_pointee_distinct_enum(&value);
}
