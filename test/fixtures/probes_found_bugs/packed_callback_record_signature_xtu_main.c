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
    packed_callback_transform_t *transform);

static struct packed_callback_payload transform_packed_callback_payload(
    const struct packed_callback_payload *value) {
  struct packed_callback_payload result = *value;
  result.value += 2;
  return result;
}

int main(void) {
  return invoke_packed_callback_transform(
             transform_packed_callback_payload) == 42
             ? 0
             : 1;
}
