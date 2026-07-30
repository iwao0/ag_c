// Paired with atomic_enum_flexible_record_signature_xtu_main.c.

#ifndef AG_C_ATOMIC_ENUM_FLEXIBLE_RECORD_SIGNATURE_XTU_TYPES
#define AG_C_ATOMIC_ENUM_FLEXIBLE_RECORD_SIGNATURE_XTU_TYPES
struct atomic_enum_flexible_packet {
  int count;
  _Atomic(unsigned int) values[];
};
#endif

_Atomic(struct atomic_enum_flexible_packet *)
    shared_atomic_enum_flexible_packet = 0;

int accept_atomic_enum_flexible_packet(
    _Atomic(struct atomic_enum_flexible_packet *) packet) {
  return packet == 0 ? 20 : 0;
}
