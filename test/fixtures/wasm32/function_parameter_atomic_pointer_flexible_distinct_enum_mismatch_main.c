enum atomic_flexible_parameter_expected_enum {
  ATOMIC_FLEXIBLE_PARAMETER_EXPECTED_ZERO = 0,
  ATOMIC_FLEXIBLE_PARAMETER_EXPECTED_VALUE = 42
};

struct atomic_flexible_parameter_packet {
  int count;
  _Atomic(enum atomic_flexible_parameter_expected_enum) values[];
};

int read_atomic_pointer_flexible_enum(
    _Atomic(struct atomic_flexible_parameter_packet *) packet);

int main(void) {
  return read_atomic_pointer_flexible_enum(0);
}
