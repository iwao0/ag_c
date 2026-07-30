// Paired with record_member_signature_mismatch_main.c.  The member's unsigned
// type makes this named record incompatible with the signed definition.

struct payload {
  unsigned int value;
};

int consume_payload(struct payload value) {
  return (int)value.value;
}
