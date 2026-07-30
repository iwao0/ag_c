// Paired with function_return_atomic_pointer_flexible_enum_integer_mismatch_main.c.

struct atomic_flexible_return_packet {
  int count;
  _Atomic(int) values[];
};

_Atomic(struct atomic_flexible_return_packet *)
get_atomic_pointer_flexible_enum_integer(void) {
  return 0;
}
