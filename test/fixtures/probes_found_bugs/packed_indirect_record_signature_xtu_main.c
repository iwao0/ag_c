#pragma pack(push, 1)
#ifndef AG_C_PACKED_INDIRECT_RECORD_SIGNATURE_XTU_TYPES
#define AG_C_PACKED_INDIRECT_RECORD_SIGNATURE_XTU_TYPES
struct packed_indirect_payload {
  char tag;
  int values[4];
};
#endif
#pragma pack(pop)

_Static_assert(
    sizeof(struct packed_indirect_payload) ==
        sizeof(char) + 4 * sizeof(int),
    "packed payload must have no member padding");

int read_packed_indirect_payload(
    struct packed_indirect_payload value);

int main(void) {
  struct packed_indirect_payload value = {
      'x', {10, 20, 30, 42}};
  return read_packed_indirect_payload(value) == 42 ? 0 : 1;
}
