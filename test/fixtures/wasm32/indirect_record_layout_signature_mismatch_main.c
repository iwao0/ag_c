#pragma pack(push, 1)
struct indirect_layout_payload {
  char tag;
  int values[4];
};
#pragma pack(pop)

_Static_assert(
    sizeof(struct indirect_layout_payload) == 17,
    "packed indirect payload size");

int read_indirect_layout_payload(
    struct indirect_layout_payload value);

int main(void) {
  struct indirect_layout_payload value = {
      'x', {10, 20, 30, 42}};
  return read_indirect_layout_payload(value) == 42 ? 0 : 1;
}
