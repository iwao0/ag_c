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
    const struct packed_pointer_payload *value);

int main(void) {
  struct packed_pointer_payload second = {'b', 22, 0};
  struct packed_pointer_payload first = {'a', 20, &second};
  return sum_packed_pointer_payload(&first) == 42 ? 0 : 1;
}
