enum atomic_return_unsigned_enum {
  ATOMIC_RETURN_UNSIGNED_ZERO = 0,
  ATOMIC_RETURN_UNSIGNED_VALUE = 42
};

_Atomic(enum atomic_return_unsigned_enum)
make_atomic_enum_integer_mismatch(void);

int main(void) {
  _Atomic(enum atomic_return_unsigned_enum) value =
      make_atomic_enum_integer_mismatch();
  return value;
}
