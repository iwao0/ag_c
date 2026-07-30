enum atomic_pointer_expected_enum {
  ATOMIC_POINTER_EXPECTED_ZERO = 0,
  ATOMIC_POINTER_EXPECTED_VALUE = 42
};

int read_atomic_pointer_distinct_enum(
    _Atomic(enum atomic_pointer_expected_enum *) value);

int main(void) {
  enum atomic_pointer_expected_enum value =
      ATOMIC_POINTER_EXPECTED_VALUE;
  return read_atomic_pointer_distinct_enum(&value);
}
