// A flexible array of atomic enum elements remains compatible with a flexible
// array of atomic elements using the enum's compatible integer type.
// Expected with the companion TU: exit=42.

enum atomic_flexible_unsigned_value {
  ATOMIC_FLEXIBLE_ZERO = 0,
  ATOMIC_FLEXIBLE_VALUE = 42
};

#ifndef AG_C_ATOMIC_ENUM_FLEXIBLE_RECORD_SIGNATURE_XTU_TYPES
#define AG_C_ATOMIC_ENUM_FLEXIBLE_RECORD_SIGNATURE_XTU_TYPES
struct atomic_enum_flexible_packet {
  int count;
  _Atomic(enum atomic_flexible_unsigned_value) values[];
};
#endif

int accept_atomic_enum_flexible_packet(
    _Atomic(struct atomic_enum_flexible_packet *) packet);

extern _Atomic(struct atomic_enum_flexible_packet *)
    shared_atomic_enum_flexible_packet;

int main(void) {
  _Atomic(struct atomic_enum_flexible_packet *) packet = 0;
  return accept_atomic_enum_flexible_packet(packet) +
         (shared_atomic_enum_flexible_packet == 0 ? 22 : 0);
}
