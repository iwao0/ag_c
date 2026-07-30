// Paired with function_parameter_atomic_pointer_flexible_distinct_enum_mismatch_main.c.

enum atomic_flexible_parameter_actual_enum {
  ATOMIC_FLEXIBLE_PARAMETER_ACTUAL_ZERO = 0,
  ATOMIC_FLEXIBLE_PARAMETER_ACTUAL_VALUE = 42
};

struct atomic_flexible_parameter_packet {
  int count;
  _Atomic(enum atomic_flexible_parameter_actual_enum) values[];
};

int read_atomic_pointer_flexible_enum(
    _Atomic(struct atomic_flexible_parameter_packet *) packet) {
  return packet == 0 ? 42 : packet->count;
}
