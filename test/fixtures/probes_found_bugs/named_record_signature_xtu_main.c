// Named records defined in separate translation units are compatible when
// their tags and corresponding members match.  A pointer to an incomplete
// record also remains compatible with the completed record in the companion
// translation unit.

#ifndef AG_C_NAMED_RECORD_SIGNATURE_XTU_TYPES
#define AG_C_NAMED_RECORD_SIGNATURE_XTU_TYPES
struct packet {
  int value;
};

struct envelope;
#endif

int read_packet(struct packet value);
struct envelope *get_envelope(void);
int read_envelope(const struct envelope *value);

int main(void) {
  struct packet packet = {20};
  return read_packet(packet) +
         read_envelope(get_envelope());
}
