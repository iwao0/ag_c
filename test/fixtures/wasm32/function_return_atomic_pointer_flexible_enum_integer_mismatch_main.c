enum atomic_flexible_return_unsigned_enum {
  ATOMIC_FLEXIBLE_RETURN_ZERO = 0,
  ATOMIC_FLEXIBLE_RETURN_VALUE = 42
};

struct atomic_flexible_return_packet {
  int count;
  _Atomic(enum atomic_flexible_return_unsigned_enum) values[];
};

_Atomic(struct atomic_flexible_return_packet *)
get_atomic_pointer_flexible_enum_integer(void);

int main(void) {
  return get_atomic_pointer_flexible_enum_integer() == 0
             ? 42
             : 0;
}
