struct incomplete_pointer_context;

#pragma pack(push, 1)
struct sibling_layout_payload {
  char tag;
  int values[4];
};
#pragma pack(pop)

_Static_assert(
    sizeof(struct sibling_layout_payload) == 17,
    "packed sibling payload size");

int read_sibling_layout_payload(
    struct incomplete_pointer_context *context,
    struct sibling_layout_payload value);

int main(void) {
  struct sibling_layout_payload value = {
      'x', {10, 20, 30, 42}};
  return read_sibling_layout_payload(0, value) == 42 ? 0 : 1;
}
