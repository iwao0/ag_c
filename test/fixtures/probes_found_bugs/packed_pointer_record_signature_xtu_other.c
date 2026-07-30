#pragma pack(push, 1)
#ifndef AG_C_PACKED_POINTER_RECORD_SIGNATURE_XTU_TYPES
#define AG_C_PACKED_POINTER_RECORD_SIGNATURE_XTU_TYPES
struct packed_pointer_payload {
  char tag;
  int value;
  struct packed_pointer_payload *next;
};
#endif
#pragma pack(pop)

_Static_assert(
    sizeof(struct packed_pointer_payload) ==
        sizeof(char) + sizeof(int) + sizeof(void *),
    "packed recursive payload must have no member padding");

int sum_packed_pointer_payload(
    const struct packed_pointer_payload *value) {
  int next_value = value->next ? value->next->value : 0;
  return value->value + next_value;
}
