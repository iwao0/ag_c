#pragma pack(push, 1)
struct nested_pointer_payload {
  char tag;
  int value;
};
#pragma pack(pop)

struct nested_pointer_holder {
  struct nested_pointer_payload payload;
  unsigned char tail[8];
};

struct nested_pointer_envelope {
  const struct nested_pointer_payload *payload;
  long long outer_layout_anchor;
};

_Static_assert(
    sizeof(struct nested_pointer_payload) == 5,
    "packed nested pointer payload size");
_Static_assert(
    _Alignof(struct nested_pointer_envelope) == _Alignof(long long),
    "outer envelope alignment must match in both translation units");

int read_nested_pointer_envelope(
    const struct nested_pointer_envelope *value);

int main(void) {
  struct nested_pointer_holder holder = {{'x', 42}, {0}};
  struct nested_pointer_envelope envelope = {
      &holder.payload, 0};
  return read_nested_pointer_envelope(&envelope) == 42 ? 0 : 1;
}
