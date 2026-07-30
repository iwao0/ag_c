#pragma pack(push, 1)
#ifndef AG_C_PACKED_GLOBAL_CALLBACK_SIGNATURE_XTU_TYPES
#define AG_C_PACKED_GLOBAL_CALLBACK_SIGNATURE_XTU_TYPES
struct packed_global_callback_payload {
  char tag;
  int value;
};
#endif
#pragma pack(pop)

typedef int packed_global_callback_t(
    const struct packed_global_callback_payload *value);

static int read_packed_global_callback(
    const struct packed_global_callback_payload *value) {
  return value->value;
}

packed_global_callback_t *packed_global_callback =
    read_packed_global_callback;
