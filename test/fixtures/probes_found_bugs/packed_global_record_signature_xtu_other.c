#pragma pack(push, 1)
#ifndef AG_C_PACKED_GLOBAL_RECORD_SIGNATURE_XTU_TYPES
#define AG_C_PACKED_GLOBAL_RECORD_SIGNATURE_XTU_TYPES
struct packed_global_payload {
  char tag;
  int value;
};
#endif
#pragma pack(pop)

_Static_assert(
    sizeof(struct packed_global_payload) ==
        sizeof(char) + sizeof(int),
    "packed global payload must have no member padding");

struct packed_global_payload packed_global_value = {'x', 42};
