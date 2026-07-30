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

extern packed_global_callback_t *packed_global_callback;

int main(void) {
  struct packed_global_callback_payload value = {'x', 42};
  return packed_global_callback(&value) == 42 ? 0 : 1;
}
