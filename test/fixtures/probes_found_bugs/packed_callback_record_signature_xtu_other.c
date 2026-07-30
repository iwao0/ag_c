#pragma pack(push, 1)
#ifndef AG_C_PACKED_CALLBACK_RECORD_SIGNATURE_XTU_TYPES
#define AG_C_PACKED_CALLBACK_RECORD_SIGNATURE_XTU_TYPES
struct packed_callback_payload {
  char tag;
  int value;
};
#endif
#pragma pack(pop)

_Static_assert(
    sizeof(struct packed_callback_payload) ==
        sizeof(char) + sizeof(int),
    "packed callback payload must have no member padding");

typedef struct packed_callback_payload packed_callback_transform_t(
    const struct packed_callback_payload *value);

int invoke_packed_callback_transform(
    packed_callback_transform_t *transform) {
  struct packed_callback_payload input = {'x', 40};
  struct packed_callback_payload result = transform(&input);
  return result.value;
}
