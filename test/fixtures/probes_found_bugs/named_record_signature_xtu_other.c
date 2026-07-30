// Paired with named_record_signature_xtu_main.c.

#ifndef AG_C_NAMED_RECORD_SIGNATURE_XTU_TYPES
#define AG_C_NAMED_RECORD_SIGNATURE_XTU_TYPES
struct packet {
  int value;
};

struct envelope {
  int value;
};
#endif

int read_packet(struct packet value) {
  return value.value;
}

static struct envelope stored = {22};

struct envelope *get_envelope(void) {
  return &stored;
}

int read_envelope(const struct envelope *value) {
  return value->value;
}
