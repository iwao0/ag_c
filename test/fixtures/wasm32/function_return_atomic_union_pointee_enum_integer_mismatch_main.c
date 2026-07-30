enum atomic_union_return_unsigned_enum {
  ATOMIC_UNION_RETURN_ZERO = 0,
  ATOMIC_UNION_RETURN_VALUE = 42
};

union atomic_union_return_payload {
  enum atomic_union_return_unsigned_enum value;
  unsigned int bits;
};

_Atomic(union atomic_union_return_payload) *
get_atomic_union_pointee_enum_integer(void);

int main(void) {
  union atomic_union_return_payload snapshot =
      *get_atomic_union_pointee_enum_integer();
  return snapshot.value;
}
