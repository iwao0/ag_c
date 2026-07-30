#pragma pack(push, 1)
struct pointer_layout_payload {
  char tag;
  int value;
};
#pragma pack(pop)

struct pointer_layout_holder {
  struct pointer_layout_payload payload;
  unsigned char tail[8];
};

_Static_assert(
    sizeof(struct pointer_layout_payload) == 5,
    "packed pointer payload size");

int read_pointer_layout_payload(
    const struct pointer_layout_payload *value);

int main(void) {
  struct pointer_layout_holder holder = {{'x', 42}, {0}};
  return read_pointer_layout_payload(&holder.payload) == 42 ? 0 : 1;
}
